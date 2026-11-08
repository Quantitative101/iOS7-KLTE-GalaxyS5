#!/bin/sh
# Build the two shim dylibs. Run from anywhere.
#   j1shim.dylib : thin loader, goes into the root image (tools/fast-shim.sh), injected into backboardd.
#   j1gl.dylib   : swappable GL code (fake macro context + software GLES), delivered at runtime via tools/push-gl.sh.
set -e
# Needs sdk/ = an iPhoneOS SDK sysroot with OpenGLES, libSystem and libobjc (from Xcode's iPhoneOS SDK, not in this repo).
cd "$(dirname "$0")"
P=..
python3 -I gen-trace.py gl/glnames.txt
python3 -I gen-ctx.py gl/gl-dispatch.txt gl/glnames.txt
CF="-target armv7-apple-ios7.0 -marm -ffreestanding -fno-stack-protector -O${J1_OPT:-2} ${J1_CFLAGS:-}"
for f in trace eagl impl trace_tab ctx_tab loader; do
  clang $CF -c $f.c -o $f.o 2>&1 | grep -E "error" || true
done
clang -target armv7-apple-ios7.0 -marm -c trace_stubs.s -o trace_stubs.o
clang -target armv7-apple-ios7.0 -marm -c ctx_stubs.s -o ctx_stubs.o
LF="-target armv7-apple-ios7.0 -isysroot sdk -dynamiclib -nostdlib -current_version 1.0.0 -compatibility_version 1.0.0"
clang $LF -install_name /var/tmp/j1gl.dylib trace.o eagl.o impl.o trace_tab.o ctx_tab.o trace_stubs.o ctx_stubs.o \
  -lSystem -lobjc.A -framework OpenGLES -o j1gl.dylib 2>&1 | grep -v warning || true
clang $LF -install_name /usr/lib/j1shim.dylib loader.o -lSystem -o j1shim.dylib 2>&1 | grep -v warning || true
ls -la j1gl.dylib j1shim.dylib
