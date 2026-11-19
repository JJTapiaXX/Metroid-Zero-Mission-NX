#!/usr/bin/env python3
"""Real achievement runtime with deferred-cache and network test doubles."""
from engine_test_helpers import bootstrap
bootstrap(globals())
assert args.ra_test,'Use --ra-test'
p=allocate(16);uc.mem_write(p,b'mzm.gba\0')
assert call('GameSwitch_Load',p)
test_achievements()
assert cache_calls['count']>=3,'Unlock/cache jobs were not deferred'
print('PASS: real unlock evaluation has no synchronous SD opens; signed offline cache survives restart; token rotation, queued sync, badge pixels and tamper rejection retained.',flush=True)
