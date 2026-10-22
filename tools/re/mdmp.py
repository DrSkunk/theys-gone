import struct,sys
d=open(sys.argv[1],'rb').read()
sig,ver,nstreams,stream_rva,chk,ts,flags=struct.unpack_from('<IIIIIIQ',d,0)
assert sig==0x504d444d
streams={}
for i in range(nstreams):
    t,sz,rva=struct.unpack_from('<III',d,stream_rva+i*12); streams.setdefault(t,[]).append((rva,sz))
def rstr(rva):
    n,=struct.unpack_from('<I',d,rva); return d[rva+4:rva+4+n].decode('utf-16le')
mods=[]
rva,sz=streams[4][0]; n,=struct.unpack_from('<I',d,rva)
for i in range(n):
    base,size,chk,ts,name_rva=struct.unpack_from('<QIIII',d,rva+4+i*108)
    mods.append((base,size,rstr(name_rva)))
def mod(addr):
    for b,s,nm in mods:
        if b<=addr<b+s: return f"{nm.split(chr(92))[-1]}+0x{addr-b:x}"
    return None
for b,s,nm in mods:
    if 'DaysGone' in nm or 'TheysGone' in nm or 'version.dll' in nm: print(f"MODULE {nm.split(chr(92))[-1]} base=0x{b:x} size=0x{s:x}")
# exception
rva,sz=streams[6][0]
tid,_,code,eflags,rec,addr=struct.unpack_from('<IIIIQQ',d,rva)
nparams,=struct.unpack_from('<I',d,rva+32)
params=struct.unpack_from('<15Q',d,rva+40)
ctx_sz,ctx_rva=struct.unpack_from('<II',d,rva+160)
print(f"EXCEPTION thread={tid} code=0x{code:x} addr=0x{addr:x} ({mod(addr)}) params={[hex(p) for p in params[:nparams]]}")
# context (x64 CONTEXT): Rip at offset 0xF8, Rsp 0x98, Rbp 0xA0
ctx=d[ctx_rva:ctx_rva+ctx_sz]
rip,=struct.unpack_from('<Q',ctx,0xF8); rsp,=struct.unpack_from('<Q',ctx,0x98)
regs={n:struct.unpack_from('<Q',ctx,0x78+8*i)[0] for i,n in enumerate(['rax','rcx','rdx','rbx','rsp','rbp','rsi','rdi','r8','r9','r10','r11','r12','r13','r14','r15'])}
print(f"RIP=0x{rip:x} ({mod(rip)}) RSP=0x{rsp:x}"); print("REGS", {k:hex(v) for k,v in regs.items()})
# memory ranges
mem=[]
if 9 in streams:
    rva,sz=streams[9][0]; n,base_rva=struct.unpack_from('<QQ',d,rva); off=base_rva
    for i in range(n):
        start,size=struct.unpack_from('<QQ',d,rva+16+i*16); mem.append((start,size,off)); off+=size
if 5 in streams:
    rva,sz=streams[5][0]; n,=struct.unpack_from('<I',d,rva)
    for i in range(n):
        start,size,mrva=struct.unpack_from('<QIQ',d,rva+4+i*16)[0],struct.unpack_from('<I',d,rva+4+i*16+8)[0],struct.unpack_from('<I',d,rva+4+i*16+12)[0]; mem.append((start,size,mrva))
def read(addr,size):
    for s,z,o in mem:
        if s<=addr and addr+size<=s+z: return d[o+addr-s:o+addr-s+size]
    return None
stack=read(rsp,0x4000) or read(rsp,0x1000) or b''
print(f"stack bytes={len(stack)}; candidate return addresses (exe/asi):")
exe_asi=[(b,s,nm) for b,s,nm in mods if 'DaysGone' in nm or 'TheysGone' in nm]
shown=0
for i in range(0,len(stack)-7,8):
    v,=struct.unpack_from('<Q',stack,i)
    for b,s,nm in exe_asi:
        if b<=v<b+s:
            print(f"  rsp+0x{i:x}: 0x{v:x} {nm.split(chr(92))[-1]}+0x{v-b:x}"); shown+=1
    if shown>=40: break
print("\n## all threads")
rva,sz=streams[3][0]; n,=struct.unpack_from('<I',d,rva)
for i in range(n):
    o=rva+4+i*48
    tid,susp,prio_c,prio,teb,ss_start,ss_size,ss_rva,cs_size,cs_rva=struct.unpack_from('<IIIIQQIIII',d,o)
    st=d[ss_rva:ss_rva+ss_size]
    frames=[]
    for j in range(0,len(st)-7,8):
        v,=struct.unpack_from('<Q',st,j)
        for b,s,nm in exe_asi:
            if b<=v<b+s: frames.append(f"{'asi' if 'Theys' in nm else 'exe'}+0x{v-b:x}")
    if any(f.startswith('asi') for f in frames) or tid==312:
        print(f"THREAD {tid} stack={ss_size} frames: {' '.join(frames[:60])}")
