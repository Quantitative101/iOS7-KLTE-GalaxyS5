"""Decrypt an iOS firmware root filesystem DMG ('encrcdsa' v2 header) with the published 36-byte VFDecrypt key.

Key layout: 16-byte AES-128 key followed by a 20-byte HMAC-SHA1 key. Per 512-byte block i the CBC IV is the first
16 bytes of HMAC-SHA1(hmac_key, big-endian u32 i). Header fields (big-endian): blocksize @0x34, datasize @0x38,
dataoffset @0x40.
Usage: python3 -I tools/vfdecrypt.py in.dmg out.dmg <72-hex-char key>
"""
import hashlib
import hmac
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes


def main(src, dst, hexkey):
    key = bytes.fromhex(hexkey)
    assert len(key) == 36, "expected a 36-byte (72 hex char) key"
    aes_key, hmac_key = key[:16], key[16:]
    fin = open(src, "rb")
    hdr = fin.read(0x48)
    assert hdr[:8] == b"encrcdsa", "not an encrcdsa image"
    blocksize = struct.unpack(">I", hdr[0x34:0x38])[0]
    datasize = struct.unpack(">Q", hdr[0x38:0x40])[0]
    dataoff = struct.unpack(">Q", hdr[0x40:0x48])[0]
    print("blocksize=%d datasize=%d dataoffset=0x%x" % (blocksize, datasize, dataoff))
    base = hmac.new(hmac_key, digestmod=hashlib.sha1)
    fin.seek(dataoff)
    out = open(dst, "wb")
    done = 0
    i = 0
    while done < datasize:
        chunk = fin.read(blocksize)
        if not chunk:
            break
        h = base.copy()
        h.update(struct.pack(">I", i))
        iv = h.digest()[:16]
        dec = Cipher(algorithms.AES(aes_key), modes.CBC(iv)).decryptor()
        out.write(dec.update(chunk) + dec.finalize())
        done += len(chunk)
        i += 1
        if i % 200000 == 0:
            print("%d MB" % (done >> 20), flush=True)
    out.truncate(datasize)
    out.close()
    print("wrote %s (%d bytes)" % (dst, datasize))


if __name__ == "__main__":
    main(*sys.argv[1:4])
