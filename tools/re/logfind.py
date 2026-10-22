import struct,sys,re,collections
exe=sys.argv[1]; base=0x140000000; d=open(exe,'rb').read()
pe=struct.unpack_from('<I',d,0x3c)[0]; nsec=struct.unpack_from('<H',d,pe+6)[0]; opt=pe+24; optsz=struct.unpack_from('<H',d,pe+20)[0]
secs=[]
for i in range(nsec):
    o=opt+optsz+i*40; name=d[o:o+8].rstrip(b'\0').decode(); vs,va,rs,ro=struct.unpack_from('<IIII',d,o+8); secs.append((name,va,vs,ro,rs))
text=[s for s in secs if s[0]=='.text'][0]; T=d[text[3]:text[3]+text[4]]; tva=text[1]
rd=[s for s in secs if s[0]=='.rdata'][0]
# ANSI source paths ending in .cpp under Engine\Source\Runtime\Engine
paths={}
for m in re.finditer(rb'D:\\RC_P4\\[ -~]{10,200}?\.cpp\x00', d[rd[3]:rd[3]+rd[4]]):
    paths[rd[1]+m.start()]=m.group(0)[:-1].decode()
print('ansi cpp paths',len(paths))
# lea rcx,[rip+X] where X is a path; record next call within 64 bytes
tgt=collections.Counter(); samples={}
for m in re.finditer(rb'\x48\x8d\x0d', T):
    i=m.start(); disp=struct.unpack_from('<i',T,i+3)[0]; x=tva+i+7+disp
    if x in paths:
        for j in range(i+7,i+80):
            if T[j]==0xe8:
                c=tva+j+5+struct.unpack_from('<i',T,j+1)[0]; tgt[c]+=1; samples.setdefault(c,(tva+i,paths[x])); break
for c,n in tgt.most_common(8):
    print(f"call target 0x{c:x} from {n} file-path sites, e.g. 0x{samples[c][0]:x} {samples[c][1][-60:]}")
