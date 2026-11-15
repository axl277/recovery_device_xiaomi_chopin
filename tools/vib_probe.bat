@echo off
REM Phone in OrangeFox, USB connected. Hold the phone and feel each vibration.
adb push vib_probe.sh /tmp/vib_probe.sh
adb shell "sh /tmp/vib_probe.sh 2>&1 | tee /tmp/vib_probe.txt"
adb pull /tmp/vib_probe.txt debug_vib.txt
echo.
echo Send debug_vib.txt and tell me which labels (A-xx, B-x, C-xx, D-xx) felt strongest.
pause
