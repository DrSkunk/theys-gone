import struct,sys
exe=sys.argv[1]; base=0x140000000; d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
def off(rva):
    for name,va,vs,ro,rs in secs:
        if va<=rva<va+rs: return ro+rva-va
    return None
def q(rva):
    o=off(rva); return struct.unpack_from('<Q',d,o)[0] if o else None
def is_rdata(rva):
    s=[x for x in secs if x[0]=='.rdata'][0]; return s[1]<=rva<s[1]+s[4]
def is_data(rva):
    s=[x for x in secs if x[0] in ('.data','.rdata')]; return any(x[1]<=rva<x[1]+x[4] for x in s)
for arg in sys.argv[2:]:
    slot_rva=int(arg,16)
    # walk back to find vtable start: entry before start is COL pointer (into .rdata/.data), COL.signature==1 and COL.pSelf==its own rva
    rva=slot_rva
    for k in range(4096):
        prev=q(rva-8)
        if prev and prev>base and is_data(prev-base):
            col=prev-base; o=off(col)
            sig,offs,cd,td,chd,selfrva=struct.unpack_from('<IIIIII',d,o)
            if sig==1 and selfrva==col:
                tdo=off(td); name=d[tdo+16:tdo+16+200].split(b'\0')[0].decode('latin1')
                print(f"slot RVA 0x{slot_rva:x}: vtable start 0x{rva:x} slot {(slot_rva-rva)//8} class {name} (this-offset {offs})")
                break
        rva-=8
    else:
        print(f"slot RVA 0x{slot_rva:x}: no RTTI found")
