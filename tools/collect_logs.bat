@echo off
REM Windows helper: collects vibration + screenshot debug info. Phone in OrangeFox, adb connected.
set OUT=debug_chopin.txt
echo.
echo LANGKAH: tekan Power + Volume Down bersamaan selama 2 detik (2-3 kali).
echo Setelah itu tekan tombol apa saja di sini untuk lanjut.
pause
echo === leds === > %OUT%
adb shell "ls -l /sys/class/leds/" >> %OUT% 2>&1
echo === aw8697 driver dir === >> %OUT%
adb shell "ls -l /sys/bus/i2c/drivers/aw8697_haptic/" >> %OUT% 2>&1
adb shell "ls -l /sys/bus/i2c/devices/9-005a/" >> %OUT% 2>&1
echo === find activate === >> %OUT%
adb shell "find /sys/devices /sys/class -name activate" >> %OUT% 2>&1
echo === overlay script log === >> %OUT%
adb shell "cat /tmp/aw8697_sysfs.log" >> %OUT% 2>&1
echo === vibrator node test (HP harus bergetar) === >> %OUT%
adb shell "ls -l /sys/class/leds/vibrator/" >> %OUT% 2>&1
adb shell "echo 150 > /sys/class/leds/vibrator/duration; echo 1 > /sys/class/leds/vibrator/activate" >> %OUT% 2>&1
echo === screenshot files === >> %OUT%
adb shell "ls -l /tmp" >> %OUT% 2>&1
adb shell "find /tmp /sdcard /data/media -iname '*screenshot*'" >> %OUT% 2>&1
adb shell dmesg > dmesg_full.txt 2>&1
adb pull /tmp/recovery.log recovery_log.txt
echo.
echo Selesai. Kirim 3 file ini: %OUT% , dmesg_full.txt , recovery_log.txt
pause
