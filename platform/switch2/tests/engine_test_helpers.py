"""Existing native-engine test doubles plus deferred Switch 2 cache jobs."""
from pathlib import Path

def bootstrap(namespace):
    original=Path(__file__).resolve().parents[2]/'switch/tests/run_game_smoke.py'
    source=original.read_text().split("try:\n    path=allocate(16)")[0]
    source=source.replace('def hook(name,callback):','hook_handles={}\ndef hook(name,callback):')
    source=source.replace("    def wrapped(uc_,address,size,unused): callback()\n    uc.hook_add(UC_HOOK_CODE,wrapped,begin=symbols[name],end=symbols[name])",
        "    if name in hook_handles: uc.hook_del(hook_handles[name])\n    def wrapped(uc_,address,size,unused): callback()\n    hook_handles[name]=uc.hook_add(UC_HOOK_CODE,wrapped,begin=symbols[name],end=symbols[name])")
    source=source.replace("if not any(k.endswith('/999990.rgba') for k in files):", "call('CacheOunce_Flush')\n    if not any(k.endswith('/999990.rgba') for k in files):")
    exec(compile(source,str(original),'exec'),namespace)
    pending={};cache_calls={'count':0};guard={'disk':False}
    uc=namespace['uc'];regs=namespace['regs'];hook=namespace['hook'];rv=namespace['return_value']
    def queue():
        path=namespace['string'](uc.reg_read(regs[0]))
        prefix=uc.reg_read(regs[1]);prefix_size=uc.reg_read(regs[2])
        data=uc.reg_read(regs[3]);size=uc.reg_read(regs[4]);backup=bool(uc.reg_read(regs[5]))
        pending[path]=(bytes(uc.mem_read(prefix,prefix_size)) if prefix_size else b'')+(bytes(uc.mem_read(data,size)) if size else b''),backup
        cache_calls['count']+=1;rv(1)
    def flush():
        for name,(data,backup) in pending.items():
            if backup and name in namespace['files']:namespace['files'][name+'.bak']=namespace['files'][name]
            namespace['files'][name]=bytearray(data)
        pending.clear();rv()
    hook('CacheOunce_Init',lambda:rv(1));hook('CacheOunce_Queue',queue)
    hook('CacheOunce_Flush',flush);hook('CacheOunce_Close',flush)
    hook('CacheOunce_RecordRetryNeeded',lambda:rv())
    fopen=namespace['fopen']
    def guarded_open():
        assert not guard['disk'],'Achievement evaluation performed synchronous SD I/O'
        fopen()
    hook('fopen',guarded_open)
    call=namespace['call']
    def guarded_call(name,*values):
        guard['disk']=name=='Achievements_Evaluate'
        try:return call(name,*values)
        finally:guard['disk']=False
    namespace['call']=guarded_call
    namespace['deferred_cache']=pending;namespace['cache_calls']=cache_calls
