@echo off
REM Phone in OrangeFox, USB connected.   screenshot.bat        -> normal
REM                                     screenshot.bat swap   -> if red/blue colours are swapped
set SWAP=
if /i "%~1"=="swap" set SWAP=--swap
adb shell "chopin_screencap %SWAP% -o /tmp/shot.bmp"
if errorlevel 1 goto fail
adb pull /tmp/shot.bmp shot_%RANDOM%.bmp
echo Saved as shot_*.bmp in this folder.
goto :eof
:fail
echo chopin_screencap failed - copy the lines above and send them to me.
pause
