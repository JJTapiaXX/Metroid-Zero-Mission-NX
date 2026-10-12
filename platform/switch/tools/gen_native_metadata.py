#!/usr/bin/env python3
"""Patch native state and GBA-memory projection metadata into a linked ELF.
Pointer offsets come from DWARF, not scans of arbitrary scalar words.
"""
from pathlib import Path
import struct,hashlib,json,re,subprocess
from elftools.elf.elffile import ELFFile
ALIASES={'DW_TAG_const_type','DW_TAG_typedef','DW_TAG_volatile_type','DW_TAG_restrict_type'}
def attr(d,k):
 if k in d.attributes:return d.attributes[k]
 for ref in ('DW_AT_specification','DW_AT_abstract_origin'):
  if ref in d.attributes:return attr(d.get_DIE_from_attribute(ref),k)
 return None
def type_of(d):
 if 'DW_AT_type' in d.attributes:return d.get_DIE_from_attribute('DW_AT_type')
 for ref in ('DW_AT_specification','DW_AT_abstract_origin'):
  if ref in d.attributes:return type_of(d.get_DIE_from_attribute(ref))
 return None
def unwrap(d):
 while d and d.tag in ALIASES:d=type_of(d)
 return d
def count(d):
 n=1
 for c in d.iter_children():
  if c.tag=='DW_TAG_subrange_type':
   v=c.attributes.get('DW_AT_count'); u=c.attributes.get('DW_AT_upper_bound')
   if v:n*=v.value
   elif u:n*=u.value+1
   else:raise ValueError('Unbounded array')
 return n
def size(d):
 d=unwrap(d)
 if d.tag=='DW_TAG_array_type':return count(d)*size(type_of(d))
 return d.attributes['DW_AT_byte_size'].value
def pointers(d,offset=0):
 d=unwrap(d)
 if d.tag=='DW_TAG_pointer_type':return [offset]
 if d.tag=='DW_TAG_array_type':
  t=type_of(d); inner=pointers(t); stride=size(t)
  return [offset+i*stride+x for i in range(count(d)) for x in inner]
 if d.tag in ('DW_TAG_structure_type','DW_TAG_union_type'):
  out=[]
  for c in d.iter_children():
   if c.tag=='DW_TAG_member' and type_of(c):
    off=c.attributes.get('DW_AT_data_member_location')
    if off and not isinstance(off.value,int):continue
    out+=pointers(type_of(c),offset+(off.value if off else 0))
  return sorted(set(out))
 return []
def fields(a,b,ga=0,ha=0):
 a,b=unwrap(a),unwrap(b)
 if a.tag!=b.tag:raise ValueError((a.tag,b.tag))
 if a.tag=='DW_TAG_pointer_type':return [(ga,ha,4,1)]
 if size(a)==size(b) and not pointers(b):return [(ga,ha,size(a),0)]
 if a.tag=='DW_TAG_array_type':
  ta,tb=type_of(a),type_of(b); sa,sb=size(ta),size(tb)
  return [x for i in range(count(a)) for x in fields(ta,tb,ga+i*sa,ha+i*sb)]
 if a.tag=='DW_TAG_structure_type':
  ca=[c for c in a.iter_children() if c.tag=='DW_TAG_member']
  cb=[c for c in b.iter_children() if c.tag=='DW_TAG_member']
  if len(ca)!=len(cb):raise ValueError('Members differ')
  out=[]
  for x,y in zip(ca,cb):
   oa=x.attributes.get('DW_AT_data_member_location'); ob=y.attributes.get('DW_AT_data_member_location')
   if not oa or not ob or not isinstance(oa.value,int) or not isinstance(ob.value,int):continue
   # Bitfields have matching storage layout in these game structs.
   if 'DW_AT_bit_size' in x.attributes:continue
   out+=fields(type_of(x),type_of(y),ga+oa.value,ha+ob.value)
  return out
 # Unions are copied when their size is unchanged. RA's meaningful union
 # fields are scalar; pointer-bearing unions are handled by state relocations.
 if size(a)==size(b):return [(ga,ha,size(a),0)]
 return []
def main(elf_path,root,devkit):
 elf_path,root,devkit=Path(elf_path),Path(root),Path(devkit)
 streams=[]
 try:
  f=elf_path.open('rb');streams.append(f);elf=ELFFile(f);dw=elf.get_dwarf_info()
  all_symbols=list(elf.get_section_by_name('.symtab').iter_symbols())
  symbols={s.name:s for s in all_symbols if s['st_value'] and s['st_shndx']!='SHN_UNDEF'}
  symbol_at={(s.name,s['st_value']):s for s in all_symbols}
  native={}; definitions={}
  for cu in dw.iter_CUs():
   top=cu.get_top_DIE(); path=attr(top,'DW_AT_name').value.decode().replace('\\','/')
   if not path.startswith(root.as_posix()+'/src/') or path.endswith('/src/sram/sram.c'):continue
   for d in cu.iter_DIEs():
    if d.tag!='DW_TAG_variable' or not attr(d,'DW_AT_name') or not type_of(d):continue
    name=attr(d,'DW_AT_name').value.decode();loc=attr(d,'DW_AT_location');t=type_of(d)
    if name in symbols:
     try:
      size(t)
      try:complete_old=size(native[name])
      except (KeyError,ValueError):complete_old=0
      if not complete_old:native[name]=t
     except (KeyError,ValueError):native.setdefault(name,t)
    if loc and isinstance(loc.value,(bytes,list)) and len(loc.value)==9 and loc.value[0]==3:
     address=int.from_bytes(bytes(loc.value[1:]),'little')
     definitions[(address,name)]=t
  # Definitions with common linkage may refer back to a declaration DIE.
  for name,t in native.items():
   if symbols[name]['st_info']['type']=='STT_OBJECT' and symbols[name]['st_info']['bind']=='STB_GLOBAL':
    key=(symbols[name]['st_value'],name)
    try:size(definitions[key])
    except (KeyError,ValueError):definitions[key]=t
  excluded=set(re.findall(r'"([A-Za-z_][A-Za-z_0-9]*)"',(root/'platform/3ds/source/port_state_ptrs.c').read_text().split('kNotSaved[]')[1].split('};')[0]))
  excluded.add('gNonGameplayRam')
  excluded.discard('gVBlankCallback')
  iwram={name:(int(addr,16),int(length)) for addr,length,name in re.findall(r'\{ 0x([0-9a-f]+), (\d+), (\w+) \}',(root/'platform/3ds/source/port_ra_iwram_map.c').read_text())}
  ewrams={name:0x8000+int(offset,16) for name,offset in re.findall(r'\.set\s+(\w+),gEwram\+0x([0-9a-f]+)',(root/'platform/switch/source/ewram_switch.S').read_text())}
  for name,t in native.items():
   if name in ewrams:definitions.setdefault((symbols[name]['st_value'],name),t)
  entries=[]; ptrs=[]
  for (address,name),t in sorted(definitions.items()):
   if name in excluded or name.startswith('gTrack') or name in ('gSoundQueue','gSoundCodeAPointer','gSoundCodeBPointer','gSoundCodeCPointer'):continue
   symbol=symbol_at.get((name,address))
   if not symbol or not isinstance(symbol['st_shndx'],int):continue
   section=elf.get_section(symbol['st_shndx'])
   if not section['sh_flags']&1 or section.name.startswith(('.data.rel.ro','.init_array','.fini_array')):continue
   try:n=size(t); po=pointers(t)
   except (KeyError,ValueError):continue
   if not n or n>0x40000:continue
   if name.startswith('s') and not attr(t,'DW_AT_byte_size') and not po:continue
   start=len(ptrs);ptrs+=po;entries.append((address,n,start,len(po),name))
  # ARM compiler provides the exact original field layout for the memory view.
  build=elf_path.parent/'metadata';build.mkdir(exist_ok=True)
  arm_types={}
  for file in ('globals1.c','globals2.c'):
   obj=build/(file+'.o'); command=[str(devkit/'devkitARM/bin/arm-none-eabi-gcc.exe'),'-g','-O0','-mcpu=arm7tdmi','-mthumb','-std=gnu11','-fshort-enums','-DPORT_NATIVE','-DPLATFORM_SWITCH','-DMZM_3DS','-DPLATFORM_LINUX','-DREGION_EU','-DRAM_PADDING','-fcommon','-Wno-int-conversion','-Wno-incompatible-pointer-types']
   command+=['-I'+str(root/x) for x in ('port/generated/shadow','port/generated','include','port','.')]
   command+=['-c',str(root/'src'/file),'-o',str(obj)]
   result=subprocess.run(command,capture_output=True,text=True)
   if result.returncode:raise RuntimeError(result.stderr[-5000:])
   af=obj.open('rb');streams.append(af);ad=ELFFile(af).get_dwarf_info()
   for cu in ad.iter_CUs():
    for d in cu.iter_DIEs():
     if d.tag=='DW_TAG_variable' and attr(d,'DW_AT_name') and type_of(d):arm_types[attr(d,'DW_AT_name').value.decode()]=type_of(d)
  maps=[]; copies=[]
  all_maps=dict(iwram)
  for name,gba in ewrams.items():
   if name in arm_types:
    try:all_maps[name]=(gba,size(arm_types[name]))
    except (KeyError,ValueError):pass
  for name,(gba,n) in sorted(all_maps.items(),key=lambda x:x[1][0]):
   if name not in native or name not in arm_types or name not in symbols:continue
   try:fs=fields(arm_types[name],native[name])
   except (ValueError,KeyError) as error:
    print('Unmapped RA variable:',name,str(error));fs=[]
   start=len(copies); copies+=fs
   maps.append((gba,n,symbols[name]['st_value'],start,len(fs),name))
  header_size=64; entry_bytes=b''.join(struct.pack('<QIIII48s',a,n,p,c,0,name.encode()[:47]) for a,n,p,c,name in entries)
  pointer_bytes=struct.pack('<'+str(len(ptrs))+'I',*ptrs) if ptrs else b''
  map_bytes=b''.join(struct.pack('<IIIIII',a,n,h,p,c,0) for a,n,h,p,c,name in maps)
  copy_bytes=b''.join(struct.pack('<IIII',*x) for x in copies)
  signature=hashlib.sha256(elf.get_section_by_name('.text').data()+entry_bytes+pointer_bytes).digest()[:16]
  data=struct.pack('<8sIIIIIIII16s8s',b'NXMETA1\0',len(entries),len(ptrs),len(maps),len(copies),header_size,header_size+len(entry_bytes),header_size+len(entry_bytes)+len(pointer_bytes),header_size+len(entry_bytes)+len(pointer_bytes)+len(map_bytes),signature,struct.pack('<Q',max(sec['sh_addr']+sec['sh_size'] for sec in elf.iter_sections() if sec['sh_flags']&2)))+entry_bytes+pointer_bytes+map_bytes+copy_bytes
  reserve=symbols['gNativeMetadata']; section=elf.get_section(reserve['st_shndx']);offset=section['sh_offset']+reserve['st_value']-section['sh_addr']
  if len(data)>reserve['st_size']:raise RuntimeError(f'Metadata too large: {len(data)}')
  f.close();streams.remove(f)
  with elf_path.open('r+b') as f:f.seek(offset);f.write(data);f.write(bytes(reserve['st_size']-len(data)))
  (build/'metadata.json').write_text(json.dumps({'entries':[(x[4],x[1],x[3]) for x in entries],'maps':[(x[5],x[0],x[1]) for x in maps],'bytes':len(data)},indent=2))
  print(f'Patched native metadata: {len(entries)} globals, {len(ptrs)} pointer fields, {len(maps)} IWRAM mappings')
 finally:
  for f in streams:f.close()
if __name__=='__main__':
 import sys
 main(*sys.argv[1:])
