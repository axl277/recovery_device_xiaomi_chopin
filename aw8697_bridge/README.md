# aw8697_haptic_bridge

Why: TWRP/OrangeFox `vibrate()` only writes `/sys/class/leds/vibrator/{duration,activate}`
(or `/sys/class/timed_output/vibrator/enable`). The chopin kernel has neither; the built-in
AWinic aw8697 driver exposes the same controls on the I2C device instead:
`/sys/bus/i2c/devices/9-005a/{duration,activate,activate_mode,...}`.

What it does: mounts a tmpfs on `/sys/class/leds`, restores a symlink for every LED that was
there (lcd-backlight keeps working), adds `vibrator/{duration,activate}` and forwards
what TWRP writes to the real driver. If the mount cannot be done, nothing is changed.

## Install
1. Copy this folder to the root of the device tree (`aw8697_bridge/`).
2. In `device.mk` add:
       PRODUCT_PACKAGES += \
           aw8697_haptic_bridge.recovery
3. Build. Log on the phone: `/tmp/aw8697_bridge.log`.

## Test the hardware path first (no build needed), phone in OrangeFox
    adb shell "echo 150 > /sys/bus/i2c/devices/9-005a/duration; echo 1 > /sys/bus/i2c/devices/9-005a/activate"
If it does not vibrate:
    adb shell "cat /sys/bus/i2c/devices/9-005a/activate_mode"
    adb shell "echo 1 > /sys/bus/i2c/devices/9-005a/activate_mode"   (then repeat the line above; also try 0 and 2)
When a mode vibrates, put `--mode=N` after /system/bin/aw8697_haptic_bridge in the .rc.

## Remove
Delete the folder and the PRODUCT_PACKAGES line.
