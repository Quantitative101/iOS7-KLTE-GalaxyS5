#!/bin/bash
# Build EXYNOS3475_J1XLTE XNU on macOS with Xcode clang. Usage: [QEMU=1] sh tools/build-xnu.sh
# S4MINI=1 builds the Galaxy S4 Mini variant (FB text console) into build/obj-s4. QEMU=1 routes the debug console to the QEMU virt PL011 (0x09000000) instead of the J1 UART.
# S5=1 builds the Galaxy S5 (SM-G900M, MSM8974) variant into build/obj-s5.
# TAB2=1 builds the Galaxy Tab 2 10.1 (OMAP 4430) variant on top of the S4 Mini one (osfmk/arm/j1_board.h) into build/obj-tab2.
# Host tools and clang flags are relaxed because modern clang rejects the old C/C++ in XNU 2107.
P=$(cd "$(dirname "$0")/.." && pwd)
export PATH=$P/tools/stubs:$PATH HOST_CC=$P/tools/stubs/hostcc
cd "$P/xnu-j1xlte/xnu" || exit 1
RELAX_C="-Wno-error=int-conversion -Wno-error=implicit-function-declaration -Wno-error=implicit-int -Wno-error=deprecated-declarations -Wno-error=incompatible-function-pointer-types -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-function-pointer-types -fno-stack-protector -UTARGET_OS_EMBEDDED -DTARGET_OS_EMBEDDED=0"
RELAX_CXX="-Wno-register -Wno-c++11-narrowing -Wno-error=implicit-function-declaration -Wno-implicit-function-declaration -Wno-error=deprecated-declarations -fno-stack-protector -UTARGET_OS_EMBEDDED -DTARGET_OS_EMBEDDED=0"
if [ -n "$QEMU" ]; then RELAX_C="$RELAX_C -DJ1_QEMU_PL011"; RELAX_CXX="$RELAX_CXX -DJ1_QEMU_PL011"; fi
OBJDIR=obj; SYMDIR=sym; DSTDIR=dst
if [ -n "$S4MINI" ]; then RELAX_C="$RELAX_C -DJ1_QEMU_PL011 -DJ1_S4MINI"; RELAX_CXX="$RELAX_CXX -DJ1_QEMU_PL011 -DJ1_S4MINI"; OBJDIR=obj-s4; SYMDIR=sym-s4; DSTDIR=dst-s4; fi
if [ -n "$TAB2" ]; then RELAX_C="$RELAX_C -DJ1_QEMU_PL011 -DJ1_S4MINI -DJ1_TAB2"; RELAX_CXX="$RELAX_CXX -DJ1_QEMU_PL011 -DJ1_S4MINI -DJ1_TAB2"; OBJDIR=obj-tab2; SYMDIR=sym-tab2; DSTDIR=dst-tab2; fi
if [ -n "$S5" ]; then RELAX_C="$RELAX_C -DJ1_QEMU_PL011 -DJ1_S4MINI -DJ1_S5"; RELAX_CXX="$RELAX_CXX -DJ1_QEMU_PL011 -DJ1_S4MINI -DJ1_S5"; OBJDIR=obj-s5; SYMDIR=sym-s5; DSTDIR=dst-s5; fi
# Extra defines for experiments, e.g. J1_KFLAGS=-DJ1_S5_SPMI=0 (make does not track flag changes: touch the files)
RELAX_C="$RELAX_C ${J1_KFLAGS:-}"; RELAX_CXX="$RELAX_CXX ${J1_KFLAGS:-}"
export SRCROOT=$PWD OBJROOT=$P/build/$OBJDIR SYMROOT=$P/build/$SYMDIR DSTROOT=$P/build/$DSTDIR
mkdir -p "$OBJROOT" "$SYMROOT" "$DSTROOT"
run_make() {
  make SRCROOT=$SRCROOT OBJROOT=$OBJROOT SYMROOT=$SYMROOT DSTROOT=$DSTROOT \
    TARGET_CONFIGS="development arm exynos3475_j1xlte" \
    DEPLOYMENT_TARGET_FLAGS= ARCH_FLAGS_ARM_EXYNOS3475_J1XLTE="-arch armv7 --target=armv7-apple-darwin -mthumb" \
    OTHER_CFLAGS="$RELAX_C" OTHER_CXXFLAGS="$RELAX_CXX" "$@" > "$P/build/make.log" 2>&1
}
run_make "$@"
# clang emits uniquified _memset.N/_memcpy.N references in some C++ objects; alias them to the real symbols.
if grep -qE '"_mem(set|cpy)\.[0-9]+"' "$P/build/make.log"; then
  grep -oE '"_mem(set|cpy)\.[0-9]+"' "$P/build/make.log" | tr -d '"' | sort -u \
    | sed -E 's/^(_mem[a-z]+)\.[0-9]+$/-Wl,-alias,\1,&/' | tr '\n' ' ' > "$P/build/alias.flags"
  run_make LDFLAGS_KERNEL_DEVELOPMENT="$(cat "$P/build/alias.flags")" "$@"
fi
echo "done (mach_kernel is built even if the later symbolset step fails; see build/obj/*/mach_kernel)"; grep -nE "error:|\*\*\*|No such|not found|ld:" "$P/build/make.log" | head -${ERRN:-12}
