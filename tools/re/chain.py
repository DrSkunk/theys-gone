import struct,sys,re
exe=sys.argv[1]; base=0x140000000; dump=sys.argv[2]; starts=[int(x,16) for x in sys.argv[3:]]
d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
text=[s for s in secs if s[0]=='.text'][0]; T=d[text[3]:text[3]+text[4]]; tva=text[1]
rdata=[s for s in secs if s[0]=='.rdata'][0]; R=d[rdata[3]:rdata[3]+rdata[4]]; rva0=rdata[1]
pd=[s for s in secs if s[0]=='.pdata'][0]; pdata=d[pd[3]:pd[3]+pd[4]]
funcs=[struct.unpack_from('<III',pdata,i) for i in range(0,len(pdata)-11,12)]
def bounds(rva):
    lo,hi=0,len(funcs)
    while lo<hi:
        m=(lo+hi)//2
        if funcs[m][0]<=rva: lo=m+1
        else: hi=m
    f=funcs[lo-1] if lo else None; return f if f and f[0]<=rva<f[1] else None
# index all call/jmp targets once
calls={}
for m in re.finditer(rb'[\xe8\xe9]', T):
    i=m.start(); rel=struct.unpack_from('<i',T,i+1)[0]; tgt=tva+i+5+rel
    if 0<=tgt-tva<len(T): calls.setdefault(tgt,[]).append(tva+i)
natives={}
cls=None
for line in open(dump,errors='replace'):
    if line.startswith('CLASS'): cls=line.split()[2]
    m=re.search(r'FN (\w+)\(.*native=0x([0-9a-f]+)',line)
    if m: natives.setdefault(int(m.group(2),16),[]).append(cls.split('.')[-1]+'.'+m.group(1))
def vtable_hits(rva):
    needle=struct.pack('<Q',base+rva); out=[]; i=R.find(needle)
    while i!=-1 and len(out)<4: out.append(rva0+i); i=R.find(needle,i+1)
    return out
seen=set()
def walk(rva,depth):
    f=bounds(rva); 
    if not f or f[0] in seen or depth>5: return
    seen.add(f[0])
    tag=''
    if f[0] in natives: tag+=' NATIVE:'+','.join(natives[f[0]][:3])
    vt=vtable_hits(f[0])
    if vt: tag+=' VTABLE-PTR@'+','.join(hex(x) for x in vt)
    callers=calls.get(f[0],[])
    print('  '*depth+f"func 0x{f[0]:x}-0x{f[1]:x} callers={len(callers)}{tag}")
    if tag: return
    for c in callers[:6]: walk(c,depth+1)
for s in starts: walk(s,0)
