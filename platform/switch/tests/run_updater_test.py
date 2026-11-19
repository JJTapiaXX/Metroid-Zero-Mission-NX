"""Exercise GitHub updater parsing, hash verification and installation with simulated HTTPS."""
from pathlib import Path
import json,hashlib
source=Path(__file__).with_name('run_game_smoke.py').read_text().split("try:\n    path=allocate(16)")[0]
source=source.replace('def hook(name,callback):','hook_handles={}\ndef hook(name,callback):')
source=source.replace("    def wrapped(uc_,address,size,unused): callback()\n    uc.hook_add(UC_HOOK_CODE,wrapped,begin=symbols[name],end=symbols[name])", "    if name in hook_handles: uc.hook_del(hook_handles[name])\n    def wrapped(uc_,address,size,unused): callback()\n    hook_handles[name]=uc.hook_add(UC_HOOK_CODE,wrapped,begin=symbols[name],end=symbols[name])")
exec(compile(source.replace("hook('rename',rename)",''),'helpers','exec'))
switch2='switch2' in args.elf.as_posix()
asset_name='mzmnx-switch2.nro' if switch2 else 'mzmnx.nro'
tag_name='nx-switch2-v1.4' if switch2 else 'nx-v1.4'

for name in ('mutexLock','mutexUnlock','ferror'):hook(name,lambda:return_value(0))
uc.mem_map(next_handle&~4095,0x10000) # Space for newlib's inlined FILE flag reads.
sha_contexts={}
def sha_create():sha_contexts[uc.reg_read(regs[0])]=bytearray();return_value()
def sha_update():
 context,src,size=[uc.reg_read(r) for r in regs[:3]];sha_contexts[context].extend(uc.mem_read(src,size));return_value()
def sha_get():
 context,out=[uc.reg_read(r) for r in regs[:2]];uc.mem_write(out,hashlib.sha256(sha_contexts[context]).digest());return_value()
def sha_calculate():
 out,src,size=[uc.reg_read(r) for r in regs[:3]];uc.mem_write(out,hashlib.sha256(uc.mem_read(src,size)).digest());return_value()
for name,callback in [('sha256ContextCreate',sha_create),('sha256ContextUpdate',sha_update),('sha256ContextGetHash',sha_get),('sha256CalculateHash',sha_calculate)]:hook(name,callback)
nro=Path('outputs/mzm-switch/platform/switch2/mzmnx-switch2.nro' if switch2 else 'outputs/mzm-switch/platform/switch/mzm-switch-bringup.nro').read_bytes();payload=nro
asset={'name':asset_name,'browser_download_url':f'https://github.com/JJTapiaXX/Metroid-Zero-Mission-NX/releases/download/{tag_name}/{asset_name}','digest':'sha256:'+hashlib.sha256(nro).hexdigest(),'size':len(nro),'updated_at':'2099-01-01T00:00:00Z'}
metadata={'tag_name':tag_name,'draft':False,'prerelease':False,'assets':[{'name':'unrelated.zip'},asset]};http_status=200
fail_install_flag=False
base_rename=rename
def controlled_rename():
 if fail_install_flag and string(uc.reg_read(regs[0]))==path+'.update':return_value(-1)
 else:base_rename()
hook('rename',controlled_rename)
# Rejected hosts fail without a network service or TLS call.
out=allocate(8);length=allocate(8)
for url in ('http://github.com/file','https://attacker.invalid/file','https://github.com.attacker.invalid/file'):
 p=allocate(len(url)+1);uc.mem_write(p,url.encode()+b'\0');assert call('NetworkSwitch_PublicGet',p,out,length,1024)&0xffffffff!=200

def get():
 url=string(uc.reg_read(regs[0]));output=uc.reg_read(regs[1]);size_out=uc.reg_read(regs[2]);data=json.dumps(([{'tag_name':'nx-v9.0','draft':False,'prerelease':False,'assets':[asset]}, {'tag_name':tag_name,'draft':True,'prerelease':False,'assets':[asset]}, {'tag_name':tag_name,'draft':False,'prerelease':True,'assets':[asset]}, metadata] if switch2 else metadata)).encode() if ('api.github.com/' in url) else payload
 p=allocate(len(data)+1);uc.mem_write(p,data+b'\0');uc.mem_write(output,struct.pack('<Q',p));uc.mem_write(size_out,struct.pack('<Q',len(data)));return_value(http_status)
hook('NetworkSwitch_PublicGet',get)
uc.ctl_remove_cache(symbols['NetworkSwitch_PublicGet'],symbols['NetworkSwitch_PublicGet']+4)
path='sdmc:/switch/mzm-switch2/mzmnx-switch2.nro' if switch2 else 'sdmc:/switch/mzm/mzmnx.nro';original=b'original installed NRO';files[path]=bytearray(original)
p=allocate(len(path)+1);uc.mem_write(p,path.encode()+b'\0');call('Updater_Init',p)
call('Updater_Process',1);assert string(call('Updater_Button'))=='DOWNLOAD AND INSTALL'
call('Updater_Process',2);assert bytes(files[path])==nro and bytes(files[path+'.bak'])==original
assert string(call('Updater_Button'))=='UPDATE INSTALLED'
call('Updater_Process',1);assert 'LATEST FILE' in string(call('Updater_Status'))
# Tampered transfer cannot replace the original.
files[path]=bytearray(original);payload=nro[:-1]+bytes([nro[-1]^1]);call('Updater_Process',1);call('Updater_Process',2)
assert bytes(files[path])==original and 'VERIFICATION FAILED' in string(call('Updater_Status'))
payload=nro
# Even a matching checksum must not permit an NRO for another application.
wrong=bytearray(nro);end=struct.unpack_from('<I',wrong,0x18)[0];nacp=struct.unpack_from('<Q',wrong,end+24)[0];wrong[end+nacp]^=1
payload=bytes(wrong);asset['digest']='sha256:'+hashlib.sha256(payload).hexdigest();call('Updater_Process',1);call('Updater_Process',2)
assert bytes(files[path])==original and 'VERIFICATION FAILED' in string(call('Updater_Status'))
payload=nro;asset['digest']='sha256:'+hashlib.sha256(nro).hexdigest()
if switch2:
 # Even with a Switch 2 filename/hash, a Switch 1 binary must be rejected.
 payload=Path('outputs/mzm-switch/platform/switch/mzm-switch-bringup.nro').read_bytes()
 asset['size']=len(payload);asset['digest']='sha256:'+hashlib.sha256(payload).hexdigest()
 call('Updater_Process',1);call('Updater_Process',2)
 assert bytes(files[path])==original and 'VERIFICATION FAILED' in string(call('Updater_Status'))
 payload=nro;asset['size']=len(nro);asset['digest']='sha256:'+hashlib.sha256(nro).hexdigest()
 # No fallback to Switch 1, drafts, prereleases, wrong assets or mismatched URLs.
 saved=metadata.copy()
 for change in [{'tag_name':'nx-v9.0'},{'draft':True},{'prerelease':True},{'assets':[dict(asset,name='mzmnx.nro')]},{'assets':[dict(asset,browser_download_url='https://github.com/JJTapiaXX/Metroid-Zero-Mission-NX/releases/download/nx-v1.4/mzmnx-switch2.nro')]}]:
  metadata.clear();metadata.update(saved);metadata.update(change)
  call('Updater_Process',1);assert 'NO VALID NRO' in string(call('Updater_Status'))
 metadata.clear();metadata.update(saved)
 print('PASS: Switch 2 feed rejects Switch 1 releases/binaries, drafts, prereleases, wrong filenames and mismatched release URLs',flush=True)
# An installation failure must restore the original from its backup.
base_rename=rename
def fail_install():
 if string(uc.reg_read(regs[0]))==path+'.update':return_value(-1)
 else:base_rename()
fail_install_flag=True;call('Updater_Process',1);call('Updater_Process',2)
assert bytes(files[path])==original and 'ORIGINAL RESTORED' in string(call('Updater_Status'))
# Newer local development builds must not offer an older public asset.
asset['updated_at']='2000-01-01T00:00:00Z';call('Updater_Process',1);assert 'NO NEWER' in string(call('Updater_Status'))
asset['updated_at']='2099-01-01T00:00:00Z';asset['browser_download_url']='https://attacker.invalid/mzmnx.nro';call('Updater_Process',1)
assert 'NO VALID NRO' in string(call('Updater_Status'))
assert bytes(files[path])==original
print('PASS: release asset selection, same-file detection, TLS host restrictions, SHA verification, backup/install, rollback and older-release rejection (simulated transport)',flush=True)

# General updater control must not overlap Audio settings.
from PIL import Image
clicked=[]
def action():clicked.append(1);return_value()
hook('Updater_Action',action)
# The UI check only verifies routing; audio settings persistence is tested separately.
hook('AudioFilters_Save',lambda:return_value())
# Unicorn lacks the A78 LSE compare/exchange used by libounce builds.
if switch2: hook('AudioFilters_Set',lambda:return_value())
input_ptr=allocate(32)
def tap(x,y):
 uc.mem_write(input_ptr,struct.pack('<HBBBBxxiiBxxx',0,0,1,1,1,x,y,0));return call('SwitchUI_Render',input_ptr)
tap(288,8);ui=tap(100,140);assert len(clicked)==1
Image.frombytes('RGBA',(320,240),bytes(uc.mem_read(ui,320*240*4))).save(args.output/'updater-settings.png')
tap(150,32);tap(100,140)
assert len(clicked)==1,'Audio setting triggered updater'
print('PASS: updater button is in General, independent of Audio controls',flush=True)

# Parse actual GitHub metadata captured during this task, when available.
live=Path('work/latest-release-updater.json')
if live.exists() and not switch2:
 metadata=json.loads(live.read_text(encoding='utf-8-sig'));call('Updater_Process',1)
 assert 'NO NEWER' in string(call('Updater_Status')) or 'LATEST FILE' in string(call('Updater_Status'))
 print('PASS: actual GitHub latest-release metadata parsed',flush=True)
