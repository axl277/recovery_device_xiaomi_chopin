# aw8697_haptic_bridge v2

TWRP/OrangeFox `vibrate()` writes `/sys/class/leds/vibrator/{duration,activate}`; the chopin
kernel has neither that node nor `timed_output`. The built-in AWinic aw8697 driver exposes
the controls on the I2C device (`/sys/bus/i2c/devices/9-005a/`).

This daemon creates the node TWRP expects (tmpfs on /sys/class/leds, every real LED relinked,
so lcd-backlight keeps working) and forwards each vibration to the driver:

* the OrangeFox vibration slider (10..300) is converted to **strength** -> driver attribute `gain`
* a fixed pulse (default 60 ms) is written to the real `duration`, then `activate` = 1

If the mount fails, nothing is changed. Log: `/tmp/aw8697_bridge.log`

## Live tuning (no rebuild)
    adb shell "echo 'attr=gain fmt=hex min=0x40 max=0xff pulse=60' > /tmp/aw8697_bridge.conf"
or `tools\vib_tune.bat 0x40 0xff 60`. Applied on the next vibration.

| key | meaning | default |
|---|---|---|
| attr | strength attribute (`none` = off) | gain |
| fmt | `hex` (matches kernel %x) or `dec` | hex |
| min / max | attribute value at slider min / max | 0x40 / 0xff |
| smin / smax | slider range | 10 / 300 |
| pulse | real duration in ms (0 = pass the slider value) | 60 |
| mode / index | force activate_mode / index every time | untouched |

Numbers accept `0x..` hex or decimal. Run `tools\vib_probe.bat` to find which attribute
and range really change the strength on your unit.
