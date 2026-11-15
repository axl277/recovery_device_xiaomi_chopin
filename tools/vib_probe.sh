#!/system/bin/sh
# Hold the phone in your hand. Each test prints a label, then vibrates once.
# Note which labels feel strongest.
D=/sys/bus/i2c/devices/9-005a
[ -f $D/activate ] || for x in /sys/bus/i2c/drivers/aw8697_haptic/*; do [ -f $x/activate ] && D=$x && break; done
echo "### device dir: $D"
show() { for a in "$@"; do printf '%-16s' "$a"; if [ -f $D/$a ]; then head -3 $D/$a 2>&1 | tr '\n' ' '; else printf '(missing)'; fi; echo; done; }
vib() { echo $1 > $D/duration; echo 1 > $D/activate; }
pause() { sleep 2; }
echo; echo "### initial values"
show activate_mode index effect_id gain gain_scale vmax duration loop seq ram_vbat_comp cont_drv_lvl f0
echo; echo "=== A: gain sweep (plain hex digits), duration 200 ms ==="
for g in 10 20 40 60 80 a0 c0 ff; do
  echo "[A-$g] gain=$g"; echo $g > $D/gain; echo "      readback: $(head -1 $D/gain)"; vib 200; pause
done
echo; echo "=== B: waveform index sweep (gain ff), duration 200 ms ==="
echo ff > $D/gain
for i in 1 2 3 4 5 6 7 8; do
  echo "[B-$i] index=$i"; echo $i > $D/index; echo "      readback: $(head -1 $D/index)"; vib 200; pause
done
echo; echo "=== C: duration sweep (gain ff, index 1) ==="
echo 1 > $D/index
for d in 20 60 150 300 600; do echo "[C-$d] duration=$d"; vib $d; pause; done
echo; echo "=== D: CONT mode (activate_mode=1) ==="
echo 1 > $D/activate_mode
for d in 100 300; do echo "[D-$d] activate_mode=1 duration=$d"; vib $d; pause; done
echo 0 > $D/activate_mode
echo; echo "### final values"; show activate_mode index gain
echo "### done - reboot the recovery to restore the original values"
