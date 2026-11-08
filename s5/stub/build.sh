#!/bin/bash
# Build the S5 XNU boot image: stub + flattened J1_S5 kernel + XNU device tree, wrapped in a clone of the stock boot image
# (QCDT kept).   bash s5/stub/build.sh ref/s5-backup/boot.img [MACH_KERNEL]
set -e
D=$(cd "$(dirname "$0")" && pwd)
P=$(cd "$D/../.." && pwd)
STOCK=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
K=${2:-$P/ports/build/obj-s5/DEVELOPMENT_ARM_EXYNOS3475_J1XLTE/mach_kernel}
[ -f "$STOCK" ] || { echo "need the stock boot image" >&2; exit 1; }
[ -f "$K" ] || { echo "no S5 kernel at $K (S5=1 bash ports/tools/build-xnu.sh)" >&2; exit 1; }
W=$P/s5/build
mkdir -p "$W"
python3 -I "$P/ports/tools/flatten-macho.py" "$K" "$W/xnu.img"
python3 -I "$P/ports/tools/make-dt.py" "$W/dtb"   # minimal Apple-format device tree (model QEMU-virt)
# MobileGestalt's main-screen-scale comes from IODeviceTree:/chosen "display-scale" (0 without it: SpringBoard aborts)
python3 -I "$P/s5/tools/dt-addprop.py" "$W/dtb" chosen display-scale=1 display-rotation=0
cd "$W"
CF="-march=armv7-a -mfloat-abi=soft -marm -Os -ffreestanding -fno-builtin -nostdlib"
arm-none-eabi-gcc $CF -c "$D/stub.c" -o stub_c.o
arm-none-eabi-gcc $CF -c "$D/stub.S" -o stub_s.o
arm-none-eabi-ld -T "$D/stub.ld" stub_s.o stub_c.o -o stub.elf
arm-none-eabi-objcopy -O binary stub.elf stub.bin
arm-none-eabi-size stub.elf
python3 -I "$P/s5/tools/mkboot-qcdt.py" "$STOCK" stub.bin "$W/xnu-boot.img"
