import struct,sys,re,subprocess
exe=sys.argv[1]; base=0x140000000; target=int(sys.argv[2],16); d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
text=[s for s in secs if s[0]=='.text'][0]; T=d[text[3]:text[3]+text[4]]; tva=text[1]
pd=[s for s in secs if s[0]=='.pdata'][0]; pdata=d[pd[3]:pd[3]+pd[4]]
funcs=[struct.unpack_from('<III',pdata,i) for i in range(0,len(pdata)-11,12)]
def bounds(rva):
    lo,hi=0,len(funcs)
    while lo<hi:
        m=(lo+hi)//2
        if funcs[m][0]<=rva: lo=m+1
        else: hi=m
    f=funcs[lo-1] if lo else None; return f if f and f[0]<=rva<f[1] else None
for m in re.finditer(rb'[\xe8\xe9]', T):
    i=m.start(); rel=struct.unpack_from('<i',T,i+1)[0]; tgt=tva+i+5+rel
    if tgt==target:
        rva=tva+i; f=bounds(rva); print(f"CALL at 0x{rva:x} in func {hex(f[0]) if f else '?'}-{hex(f[1]) if f else '?'}")
