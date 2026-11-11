#
#	This file is part of the OrangeFox Recovery Project
# Copyright (C) 2023 The OrangeFox Recovery Project
#
# NOTE: variables that start with "FOX_" must NOT be placed here, they
# only work from vendorsetup.sh (see the OrangeFox wiki: "Building OrangeFox").
#

# OrangeFox-specific settings
OF_NO_TREBLE_COMPATIBILITY_CHECK := 1
OF_NO_MIUI_PATCH_WARNING := 1

# Decryption (FBE + metadata encryption)
OF_FBE_METADATA_MOUNT_IGNORE := 1
# chopin uses the 4.0 keymaster services (beanpod / mitee)
OF_DEFAULT_KEYMASTER_VERSION := 4.0

# OrangeFox GUI settings (1080x2400, centered punch-hole, rounded corners)
OF_SCREEN_H := 2400
OF_STATUS_H := 80
OF_STATUS_INDENT_LEFT := 48
OF_STATUS_INDENT_RIGHT := 48
OF_CLOCK_POS := 1

# Flashlight (Disabled)
OF_FLASHLIGHT_ENABLE := 0

# Vibration (Disabled)
TW_NO_VIBRATION := true

# Device stuff
OF_USE_LEGACY_BATTERY_SERVICES := 1
OF_FORCE_PREBUILT_KERNEL := 1
OF_ENABLE_LPTOOLS := 1
OF_ENABLE_ALL_PARTITION_TOOLS := 1
OF_QUICK_BACKUP_LIST := /boot;/data;/dtbo;
OF_WIPE_METADATA_AFTER_DATAFORMAT := 1

# Maximum permissible splash image size (in kilobytes); do "NOT" increase
OF_SPLASH_MAX_SIZE := 130

# NOTE: OF_USE_LZ4_COMPRESSION is intentionally NOT set. The chopin kernel has
# "# CONFIG_RD_LZ4 is not set", so an lz4 ramdisk would not boot.
# NOTE: OF_ENABLE_FS_COMPRESSION is intentionally NOT set (kernel has no
# CONFIG_F2FS_FS_COMPRESSION).
