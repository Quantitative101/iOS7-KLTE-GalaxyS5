#!/bin/sh
# touch-scope.sh - read-only touchscreen scoping on the S5 while it runs TWRP (Linux has a working touch driver there).
#   adb push s5/tools/touch-scope.sh /tmp/ && adb shell sh /tmp/touch-scope.sh > s5/build/touch-scope.txt
# Prints the I2C devices and their buses (finds the MAX77826 that the device tree leaves out), the touch driver's sysfs,
# regulators that feed it, GPIO/pinmux state, its interrupt, input capabilities, and 10 s of raw events: touch the
# screen while it runs. Writes nothing.

echo "== i2c devices"
for d in /sys/bus/i2c/devices/*; do
    n=$(cat "$d/name" 2>/dev/null)
    echo "$(basename "$d") ${n} -> $(readlink "$d" | sed 's|.*/devices/||')"
done

echo "== touch driver"
for d in /sys/bus/i2c/devices/*; do
    case "$(cat "$d/name" 2>/dev/null)" in
    *synaptics* | *rmi* | *tsp* | *touch*)
        echo "-- $d"
        ls "$d" 2>/dev/null | tr '\n' ' '
        echo
        for f in "$d"/*; do
            [ -f "$f" ] && [ -r "$f" ] || continue
            case "$f" in *uevent | *modalias) continue ;; esac
            v=$(head -c 200 "$f" 2>/dev/null | tr '\n' ' ')
            echo "  $(basename "$f") = $v"
        done
        ;;
    esac
done
for c in /sys/class/sec/tsp /sys/class/sec/sec_touchscreen; do
    [ -d "$c" ] || continue
    echo "-- $c"
    for f in "$c"/*; do
        [ -f "$f" ] && [ -r "$f" ] || continue
        echo "  $(basename "$f") = $(head -c 200 "$f" 2>/dev/null | tr '\n' ' ')"
    done
done

echo "== regulators"
for r in /sys/class/regulator/*; do
    n=$(cat "$r/name" 2>/dev/null)
    case "$n" in
    *lvs* | *77826* | *ldo13* | *tsp* | *touch* | *LDO13*)
        echo "$(basename "$r") $n state=$(cat "$r/state" 2>/dev/null) uV=$(cat "$r/microvolts" 2>/dev/null) users=$(cat "$r/num_users" 2>/dev/null) dev=$(readlink "$r/device" | sed 's|.*/devices/||')"
        ;;
    esac
done
ls /sys/kernel/debug/regulator 2>/dev/null | grep -i -e lvs -e 77826 -e ldo13 | head

echo "== gpio"
if [ -r /sys/kernel/debug/gpio ]; then
    grep -i -e 'gpio-6 ' -e 'gpio-7 ' -e tsp -e touch -e synap -e pma8084 /sys/kernel/debug/gpio | head -40
else
    echo "(no debugfs gpio; trying mount)"
    mount -t debugfs none /sys/kernel/debug 2>/dev/null && grep -i -e 'gpio-6 ' -e 'gpio-7 ' -e tsp -e touch -e synap /sys/kernel/debug/gpio | head -40
fi
ls /sys/kernel/debug/ 2>/dev/null | tr '\n' ' '
echo

echo "== interrupts"
grep -i -e synap -e tsp -e touch -e rmi -e qup /proc/interrupts

echo "== input devices"
cat /proc/bus/input/devices
for e in /dev/input/event*; do
    getevent -p "$e" 2>/dev/null | grep -q -i -e 0035 -e ABS_MT && getevent -lp "$e" 2>/dev/null
done

echo "== kernel log"
dmesg | grep -i -e synap -e rmi -e tsp -e touch -e 77826 -e lvs2 | tail -60

echo "== device tree (live)"
for p in /proc/device-tree/soc/i2c@f9924000 /proc/device-tree/i2c@f9924000; do
    [ -d "$p" ] && find "$p" -maxdepth 2 | head -40
done

echo "== 10 s of events (touch the screen now)"
timeout 10 getevent -lt 2>/dev/null | grep -v -e SYN_REPORT | head -200
echo "== done"
