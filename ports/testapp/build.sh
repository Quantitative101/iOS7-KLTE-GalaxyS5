#!/bin/sh
# Build ports/testapp/fbtest: the payload reload daemon that replaces /usr/sbin/fairplayd.O2 on the S5 root. It reads the
# renderer payload from the MBR partition 3 (/dev/rdisk0s3) and installs it as /var/tmp/j1gl.dylib. Freestanding: no SDK.
#   sh ports/testapp/build.sh
set -e
cd "$(dirname "$0")"
# -fno-builtin: at -O2 clang otherwise turns the copy loops into memset/memcpy calls, which a -nostdlib binary lacks.
clang -target armv7-apple-ios7.0 -marm -ffreestanding -fno-stack-protector -fno-builtin -O2 -c fbtest.c -o fbtest.o
clang -target armv7-apple-ios7.0 -nostdlib -static -Wl,-e,_start fbtest.o -o fbtest 2>&1 | grep -v -e sysroot -e '^$' || true
[ -s fbtest ]
ls -l fbtest
