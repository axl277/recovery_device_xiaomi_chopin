#!/system/bin/sh
# Make TWRP/OrangeFox vibrate on chopin (AWinic aw8697, built into the kernel).
#
# TWRP's vibrate() only writes /sys/class/leds/vibrator/{duration,activate}. This kernel's
# aw8697 driver does not register that LED node, so nothing ever reaches the driver.
# We build a tmpfs copy of /sys/class/leds (symlinks to every real LED + a "vibrator" dir whose
# files are symlinks to the driver's real attributes) and bind it over /sys/class/leds.
# Safety: nothing is mounted unless every link resolves, and the mount is undone if the
# backlight node disappears. Log: /tmp/aw8697_sysfs.log
SYS="${SYS:-/sys}"
OV="${OV:-/tmp/aw_leds}"
LOG="${LOG:-/tmp/aw8697_sysfs.log}"
log() { echo "$*" >> "$LOG"; }
: > "$LOG"

[ -f "$SYS/class/leds/vibrator/activate" ] && { log "vibrator node already exists, nothing to do"; exit 0; }

# 1. find the real attribute directory (needs both activate and duration)
D=""
for d in "$SYS"/bus/i2c/drivers/aw8697_haptic/*; do
  [ -f "$d/activate" ] && [ -f "$d/duration" ] && { D="$d"; break; }
done
if [ -z "$D" ]; then
  for d in "$SYS"/class/leds/*; do
    [ -f "$d/activate" ] && [ -f "$d/duration" ] && { D="$d"; break; }
  done
fi
if [ -z "$D" ]; then
  for f in $(find "$SYS"/devices -maxdepth 6 -name activate 2>/dev/null); do
    d="${f%/activate}"
    [ -f "$d/duration" ] && { D="$d"; break; }
  done
fi
if [ -z "$D" ]; then
  log "FAILED: no directory with activate+duration found. Diagnostics:"
  log "--- ls /sys/class/leds";                       ls -l "$SYS"/class/leds/ >> "$LOG" 2>&1
  log "--- ls drivers/aw8697_haptic";                 ls -l "$SYS"/bus/i2c/drivers/aw8697_haptic/ >> "$LOG" 2>&1
  for d in "$SYS"/bus/i2c/drivers/aw8697_haptic/*; do log "--- ls $d"; ls -l "$d/" >> "$LOG" 2>&1; done
  log "--- find activate";                            find "$SYS"/devices "$SYS"/class -name activate >> "$LOG" 2>&1
  exit 0
fi
log "attribute dir: $D"

# 2. build the overlay
rm -rf "$OV"; mkdir -p "$OV/vibrator" || { log "FAILED: mkdir $OV"; exit 0; }
LEDS=""
for l in "$SYS"/class/leds/*; do
  [ -e "$l" ] || continue
  n="${l##*/}"
  t="$(readlink "$l")"
  case "$t" in
    /*)       abs="$t" ;;
    ../../*)  abs="$SYS/${t#../../}" ;;
    *)        abs="$SYS/class/leds/$t" ;;
  esac
  ln -s "$abs" "$OV/$n" || { log "FAILED: ln $n"; exit 0; }
  [ -e "$OV/$n" ] || { log "FAILED: link $n -> $abs does not resolve"; exit 0; }
  LEDS="$LEDS $n"
done
for a in activate duration state activate_mode index ram_update; do
  [ -e "$D/$a" ] && ln -s "$D/$a" "$OV/vibrator/$a"
done
[ -e "$OV/vibrator/activate" ] && [ -e "$OV/vibrator/duration" ] || { log "FAILED: vibrator links do not resolve"; exit 0; }
log "overlay ok, leds:$LEDS"

[ -n "$DRY_RUN" ] && { log "DRY_RUN: not mounting"; exit 0; }

# 3. mount, verify, roll back on any problem
mount -o bind "$OV" "$SYS/class/leds" >> "$LOG" 2>&1 || { log "FAILED: mount"; exit 0; }
ok=1
[ -e "$SYS/class/leds/vibrator/activate" ] || ok=0
for n in $LEDS; do [ -e "$SYS/class/leds/$n" ] || ok=0; done
if [ "$ok" = 1 ]; then
  log "mounted OK: $SYS/class/leds/vibrator -> $D"
else
  umount "$SYS/class/leds" >> "$LOG" 2>&1
  log "FAILED after mount, rolled back"
fi
exit 0
