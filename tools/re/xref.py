import struct,sys
exe=sys.argv[1]; base=0x140000000; d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
def rd(rva,n):
    for name,va,vs,ro,rs in secs:
        if va<=rva<va+rs: return d[ro+rva-va:ro+rva-va+n]
    return b''
text=[s for s in secs if s[0]=='.text'][0]; T=d[text[3]:text[3]+text[4]]; tva=text[1]
pd=[s for s in secs if s[0]=='.pdata'][0]; pdata=rd(pd[1],pd[4])
funcs=[struct.unpack_from('<III',pdata,i) for i in range(0,len(pdata)-11,12)]
def bounds(rva):
    lo,hi=0,len(funcs)
    while lo<hi:
        m=(lo+hi)//2
        if funcs[m][0]<=rva: lo=m+1
        else: hi=m
    f=funcs[lo-1] if lo else None; return f if f and f[0]<=rva<f[1] else None
def find_str(s, wide):
    needle=s.encode('utf-16le') if wide else s.encode()
    needle=needle+(b'\0\0' if wide else b'\0')
    hits=[]
    for name,va,vs,ro,rs in secs:
        off=d.find(needle,ro,ro+rs)
        while off!=-1 and off<ro+rs:
            if (not wide and (off==0 or d[off-1]==0)) or (wide and d[off-2:off]==b'\0\0'): hits.append(va+off-ro)
            off=d.find(needle,off+1,ro+rs)
    return hits
# collect rip-relative lea/mov targets in .text
targets={}
import re
for m in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', T):
    i=m.start(); disp=struct.unpack_from('<i',T,i+3)[0]; tgt=tva+i+7+disp
    targets.setdefault(tgt,[]).append(tva+i)
for arg in sys.argv[2:]:
    wide=arg.startswith('L:'); s=arg[2:] if wide else arg
    for srva in find_str(s,wide):
        refs=targets.get(srva,[])
        print(f"STRING {arg!r} at 0x{srva:x}: {len(refs)} refs")
        for r in refs[:12]:
            f=bounds(r); print(f"   ref 0x{r:x} in func {hex(f[0]) if f else '?'}-{hex(f[1]) if f else '?'}")
