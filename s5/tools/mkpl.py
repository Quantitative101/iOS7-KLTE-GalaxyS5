import struct,sys
d=open(sys.argv[1],'rb').read(); seq=int(sys.argv[2],16)
h=b'J1GL'+struct.pack('<II',seq,len(d)); h+=b'\0'*(512-len(h))
p=h+d; p+=b'\0'*((-len(p))%512)
open(sys.argv[3],'wb').write(p); print(len(p)//512,'sectors')
