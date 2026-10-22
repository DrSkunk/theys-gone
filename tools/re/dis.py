import struct,subprocess,sys,re
exe=sys.argv[1]; base=0x140000000
d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
def rd(rva,n):
    for name,va,vs,ro,rs in secs:
        if va<=rva<va+rs: return d[ro+rva-va:ro+rva-va+n]
    return b''
# pdata function bounds
pd=[s for s in secs if s[0]=='.pdata'][0]; pdata=rd(pd[1],pd[4])
funcs=[struct.unpack_from('<III',pdata,i) for i in range(0,len(pdata)-11,12)]
def bounds(rva):
    lo,hi=0,len(funcs)
    while lo<hi:
        m=(lo+hi)//2
        if funcs[m][0]<=rva: lo=m+1
        else: hi=m
    f=funcs[lo-1]; return f if f[0]<=rva<f[1] else None
def strat(addr):
    rva=addr-base; b=rd(rva,160)
    if not b: return None
    if all(32<=c<127 for c in b[:6]): return repr(b.split(b'\0')[0][:120].decode('latin1'))
    if b[1]==0 and 32<=b[0]<127 and all(x==0 for x in b[1:12:2]):
        try: return 'L'+repr(b.decode('utf-16le','ignore').split('\0')[0][:120])
        except: return None
    return None
for arg in sys.argv[2:]:
    rva=int(arg,16); f=bounds(rva)
    print(f"=== RVA 0x{rva:x} function {hex(f[0]) if f else '?'}-{hex(f[1]) if f else '?'}")
    if not f: continue
    out=subprocess.run(['objdump','-d','-M','intel','--no-show-raw-insn',f'--start-address={base+f[0]:#x}',f'--stop-address={base+f[1]:#x}',exe],capture_output=True,text=True).stdout
    lines=[l for l in out.splitlines() if re.match(r'\s*[0-9a-f]+:',l)]
    refs=set()
    for l in lines:
        for m in re.finditer(r'# (0x[0-9a-f]+)|\[rip\+0x[0-9a-f]+\]\s*# (0x[0-9a-f]+)|(0x14[0-9a-f]{7})',l):
            a=m.group(1) or m.group(2) or m.group(3)
            if a: refs.add(int(a,16))
    print(f"insns={len(lines)}; string refs:")
    for a in sorted(refs):
        s=strat(a)
        if s: print(f"   0x{a:x}: {s}")
    print("around:")
    for l in lines:
        la=int(l.split(':')[0],16)-base
        if abs(la-rva)<=0x30: print("  "+("=>" if la==rva else "  ")+l.strip()[:140])
    print("calls:")
    for l in lines:
        if ' call ' in l: print("   "+l.strip()[:120])
