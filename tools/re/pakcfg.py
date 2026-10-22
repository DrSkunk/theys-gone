import struct,sys,zlib,re
p=sys.argv[1]; pats=sys.argv[2:]
f=open(p,'rb'); f.seek(0,2); size=f.tell(); f.seek(size-44); foot=f.read(44)
magic,ver,ioff,isz=struct.unpack_from('<IiqQ',foot,0)
print('magic %08x ver %d index off %d size %d'%(magic,ver,ioff,isz))
f.seek(ioff); idx=f.read(isz)
def rstr(b,o):
    n,=struct.unpack_from('<i',b,o); o+=4
    if n<0: s=b[o:o-2*n-2].decode('utf-16le'); o+=-2*n
    else: s=b[o:o+n-1].decode('latin1'); o+=n
    return s,o
mount,o=rstr(idx,0); cnt,=struct.unpack_from('<i',idx,o); o+=4
print('mount',mount,'entries',cnt)
want=[]
for i in range(cnt):
    name,o=rstr(idx,o)
    off,csz,usz,meth=struct.unpack_from('<qqqI',idx,o); o+=28
    o+=20 # sha1
    blocks=[]
    if meth!=0:
        nb,=struct.unpack_from('<i',idx,o); o+=4
        for _ in range(nb):
            blocks.append(struct.unpack_from('<qq',idx,o)); o+=16
    enc=idx[o]; o+=1; bsz,=struct.unpack_from('<I',idx,o); o+=4
    if any(re.search(pt,name) for pt in pats): want.append((name,off,csz,usz,meth,blocks,enc,bsz))
print('matches',len(want))
for name,off,csz,usz,meth,blocks,enc,bsz in want:
    print('==',name,'meth',meth,'enc',enc,'usz',usz,'blocks',len(blocks))
    if enc or usz>400000 or meth!=1: continue
    f.seek(off)
    hdr=f.read(53 + (4+16*len(blocks) if meth else 0) + 0)  # entry header copy; parse sizes
    hoff=off+8+8+8+4+20+(4+16*len(blocks) if meth else 0)+1+4
    if meth==0:
        f.seek(hoff); data=f.read(usz)
    else:
        out=b''
        for bs,be in blocks:
            f.seek(bs); out+=zlib.decompress(f.read(be-bs))
        data=out
    txt=data.decode('utf-8','replace')
    outp='/tmp/dg-'+name.replace('/','_')
    open(outp,'w').write(txt); print('   ->',outp,len(txt))
