#!/usr/bin/env python3
"""Host model of the automatic bomb jump (platform/3ds/source/port_auto_bomb.c).

Same physics as SamusUpdateVelocityPosition and the bomb's fuse (33 frames from the lay frame to
the frame it looks for Samus), same planner: each frame a bomb can be laid, the next LOOK frames
are tried as the moment to lay it, and it is laid now when now is the moment whose throw starts
highest, a throw counting only if it still lands one frame early or late.

    auto-bomb-sim.py [LOOK [JITTER]]     e.g. auto-bomb-sim.py 16 "[-1,0,1]"

Prints the height reached every second of a 900-frame run from the ground. Units: sub pixels (4 a
pixel), y growing downward, yVelocity positive up, as the game keeps them.
"""
import sys
sys.path.insert(0,'/tmp')
ACC=10; POSCAP=192; NEGCAP=128; BOUNCE=164
FUSE=33; LOOK=int(sys.argv[1]) if len(sys.argv)>1 else 16
COOL=7; LIMIT=4
JIT=eval(sys.argv[2]) if len(sys.argv)>2 else [-1, 0, 1]
def div8(v): return int(v/8)
def step(y,v,air,floor):
    if not air: return y,v,air
    vv=max(-NEGCAP,min(POSCAP,v))
    mv=div8(vv)
    if v>=-231: v-=ACC
    y-=mv
    if y>=floor and v<=0: y=floor; v=0; air=False
    return y,v,air
def bounces(y_s,v,air,by):
    if not (by-60 < y_s < by+108): return False
    if not air: return True
    return v<=0 and not (y_s-32>by)
def simulate(state,bombs,n,which=None):
    """state=(y,v,air,floor) now; bombs list of (y,at). Returns path, result(y at bounce of bomb which)"""
    y,v,air,floor=state; path=[]; result=None; last=-100
    for s in range(n):
        y,v,air=step(y,v,air,floor)
        path.append(y)
        for i,(by,at) in enumerate(bombs):
            if at==s and bounces(y,v,air,by):
                v=BOUNCE; air=True
                if i==which: result=y
        # (a bounce this frame sets v; later bombs at the same frame see v>0)
    return path,result
def want(state,bombs):
    path,_=simulate(state,bombs,LOOK+FUSE+3)
    best=None;besty=0
    for k in range(LOOK):
        worst=None
        for d in JIT:
            cand=bombs+[(path[k],k+FUSE+d)]
            _,r=simulate(state,cand,LOOK+FUSE+4,which=len(bombs))
            if r is None: worst=None; break
            worst=r if worst is None else max(worst,r)
        if worst is not None and (best is None or worst<besty): best=k;besty=worst
    return best==0
def run(frames,start_y=0):
    y,v,air,floor=start_y,0,False,start_y
    bombs=[]  # (y,explode_frame_abs)
    cool=0; held_prev=False; ys=[]
    # player's own first bomb at frame 0
    laid=[0]
    bombs.append((y,0+FUSE)); cool=COOL
    for f in range(1,frames):
        # decide
        rel=[(by,at-f) for (by,at) in bombs if at-f>=0]
        count=len([1 for (by,at) in bombs if at>=f-1])
        press=False
        if cool<=0 and count<LIMIT and not held_prev:
            if want((y,v,air,floor),rel): press=True
        # frame f executes: samus update, then lay bomb at post-update position, check explosions
        y,v,air=step(y,v,air,floor)
        if cool>0: cool-=1
        if press:
            bombs.append((y,f+FUSE)); cool=COOL; laid.append(f)
        for (by,at) in bombs:
            if at==f and bounces(y,v,air,by): v=BOUNCE; air=True
        held_prev=press
        ys.append(y)
    return ys,laid
ys,laid=run(900)
print("LOOK",LOOK,"bombs laid",len(laid),"final height (px up):",-ys[-1]/4, "min y",min(ys)/4)
for i in range(0,900,60): print(i,round(-ys[i-1]/4,1) if i else 0)
