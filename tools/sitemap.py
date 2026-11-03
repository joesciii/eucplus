"""Map sites of image A into a window of image B by masked context alignment.
usage: sitemap.py A.dis B.dis blo bhi site[,site...]"""
import sys,re
sys.path.insert(0,sys.argv[0].rsplit('/',1)[0])
from maskmatch import load, shape
A=load(sys.argv[1]); B=load(sys.argv[2]); blo=int(sys.argv[3],16); bhi=int(sys.argv[4],16)
Bw=[x for x in B if blo<=x[0]<bhi]; bs=[shape(m,o) for _,m,o in Bw]
ai={ad:i for i,(ad,_,_) in enumerate(A)}
for s in sys.argv[5].split(','):
    s=int(s,16); i=ai[s]; best=[]
    for j in range(len(Bw)):
        sc=0
        for d in range(-8,9):
            if 0<=i+d<len(A) and 0<=j+d<len(Bw):
                sa=shape(A[i+d][1],A[i+d][2]); sb=bs[j+d]
                sc+= 2 if sa==sb else (1 if A[i+d][1]==Bw[j+d][1] else 0)
        best.append((sc,j))
    best.sort(reverse=True)
    (sc,j),(sc2,j2)=best[0],best[1]
    print('%s -> %s score %d (next %d @ %s) | A: %s %s | B: %s %s'%(hex(s),hex(Bw[j][0]),sc,sc2,hex(Bw[j2][0]),A[i][1],A[i][2],Bw[j][1],Bw[j][2]))
