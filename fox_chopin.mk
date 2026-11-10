#
# This file is part of the OrangeFox Recovery Project
# Copyright (C) 2023-2026 The OrangeFox Recovery Project
#
# Flag list based on the OFRP agate tree (Xiaomi 11T, MT6893).
# It is not confirmed that the agate tree boots to system, so treat these flags as untested.
# Decryption/hardware flags from agate are intentionally NOT copied.
#

# --- ROM / MIUI patching behavior ---
OF_DONT_PATCH_ENCRYPTED_DEVICE := 1
OF_NO_TREBLE_COMPATIBILITY_CHECK := 1
OF_NO_MIUI_PATCH_WARNING := 1
OF_ENABLE_USB_STORAGE := 1
OF_BIND_MOUNT_SDCARD_ON_FORMAT := 1
OF_PATCH_AVB20 := 1

# --- Boot image patching ---
OF_USE_MAGISKBOOT := 1
OF_USE_MAGISKBOOT_FOR_ALL_PATCHES := 1

# --- GUI (1080x2400, 120Hz screen). Same values as agate; adjust after
#     checking the layout on the device ---
OF_SCREEN_H := 2400
OF_STATUS_H := 100
OF_STATUS_INDENT_LEFT := 48
OF_STATUS_INDENT_RIGHT := 48
OF_CLOCK_POS := 1

# Flashlight: the paths below come from agate and are NOT verified on chopin
# (check with: ls /sys/class/leds in recovery)
OF_FLASHLIGHT_ENABLE := 1
OF_FL_PATH1 := /sys/class/leds/torch-light0
OF_FL_PATH2 := /sys/class/leds/torch-light1

# --- Custom ROM OTA ---
OF_SUPPORT_ALL_PAYLOAD_OTA_UPDATES := 1
OF_KEEP_DM_VERITY_FORCED_ENCRYPTION := 1
OF_KEEP_DM_VERITY := 0
OF_FIX_OTA_UPDATE_MANUAL_FLASH_ERROR := 1

# --- Device ---
OF_FORCE_PREBUILT_KERNEL := 1
OF_ENABLE_LPTOOLS := 1
OF_ENABLE_ALL_PARTITION_TOOLS := 1
OF_QUICK_BACKUP_LIST := /boot;/data;/dtbo;
OF_WIPE_METADATA_AFTER_DATAFORMAT := 1
OF_RUN_POST_FORMAT_PROCESS := 1

# Maximum splash size (KB); do not increase
OF_SPLASH_MAX_SIZE := 130
