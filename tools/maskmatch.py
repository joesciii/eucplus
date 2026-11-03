"""Find where a function of one m68k image appears in another, ignoring numbers.
usage: maskmatch.py A.dis B.dis lo hi [k]"""
import sys, re, collections
def load(p):
    out=[]
    for l in open(p):
        m=re.match(r'\s*([0-9a-f]+):\s+(?:[0-9a-f]{4}\s)+\s*(\S+)\s*(.*)',l)
        if m: out.append((int(m.group(1),16), m.group(2), m.group(3)))
    return out
def shape(mn,ops):
    ops=re.sub(r'0x[0-9a-f]+|-?\d+','N',ops)
    ops=re.sub(r'%pc@\(N\).*','PCREL',ops)
    return mn+' '+ops
if __name__=='__main__':
  A=load(sys.argv[1]); Bd=load(sys.argv[2]); lo=int(sys.argv[3],16); hi=int(sys.argv[4],16); k=int(sys.argv[5]) if len(sys.argv)>5 else 6
  a=[shape(m,o) for ad,m,o in A if lo<=ad<hi]
  b=[shape(m,o) for ad,m,o in Bd]
  idx=collections.defaultdict(list)
  for i in range(len(b)-k): idx[tuple(b[i:i+k])].append(i)
  votes=collections.Counter()
  for i in range(len(a)-k):
      for j in idx.get(tuple(a[i:i+k]),[])[:50]:
          votes[(Bd[j][0]-0)//0x400]+=1
  for blk,n in votes.most_common(8): print(hex(blk*0x400), n, 'of', len(a)-k)
