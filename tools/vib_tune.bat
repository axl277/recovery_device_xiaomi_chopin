@echo off
REM usage: vib_tune.bat MIN MAX [PULSE_MS] [hex|dec]     example: vib_tune.bat 0x40 0xff 60
if "%~1"=="" goto usage
if "%~2"=="" goto usage
set PULSE=%~3
if "%PULSE%"=="" set PULSE=60
set FMT=%~4
if "%FMT%"=="" set FMT=hex
adb shell "echo 'attr=gain fmt=%FMT% min=%~1 max=%~2 pulse=%PULSE%' > /tmp/aw8697_bridge.conf"
adb shell "cat /tmp/aw8697_bridge.conf"
echo Applied. Tap around in OrangeFox / move the slider; it takes effect on the next vibration.
goto :eof
:usage
echo usage: vib_tune.bat MIN MAX [PULSE_MS] [hex^|dec]
echo   example: vib_tune.bat 0x40 0xff 60
