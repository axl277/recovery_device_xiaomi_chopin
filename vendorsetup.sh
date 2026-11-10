# Copyright (C) 2025-2026 OrangeFox Recovery Project
# SPDX-License-Identifier: GPL-3.0-only

FDEVICE="chopin"

fox_get_target_device() {
local chkdev=$(echo "$BASH_SOURCE" | grep -w $FDEVICE)
   if [ -n "$chkdev" ]; then
      FOX_BUILD_DEVICE="$FDEVICE"
   else
      chkdev=$(set | grep BASH_ARGV | grep -w $FDEVICE)
      [ -n "$chkdev" ] && FOX_BUILD_DEVICE="$FDEVICE"
   fi
}

if [ -z "$1" -a -z "$FOX_BUILD_DEVICE" ]; then
   fox_get_target_device
fi

if [ "$1" = "$FDEVICE" -o "$FOX_BUILD_DEVICE" = "$FDEVICE" ]; then

# Build
export TW_DEFAULT_LANGUAGE="en"
export LC_ALL="C"
export ALLOW_MISSING_DEPENDENCIES=true

# Partitions (A/B, Virtual A/B, dynamic partitions)
export FOX_AB_DEVICE=1
export FOX_VIRTUAL_AB_DEVICE=1
export FOX_RECOVERY_HAS_DYNAMIC_PARTITIONS=1
export OF_ENABLE_ALL_PARTITION_TOOLS=1

# Alternative device codename (zip installer)
export TARGET_DEVICE_ALT="choping"

# Binaries & Tools
export FOX_USE_ZSTD_BINARY=1
export FOX_USE_XZ_UTILS=1
export FOX_USE_TAR_BINARY=1
export FOX_USE_SED_BINARY=1
export FOX_USE_NANO_EDITOR=1
export FOX_USE_GREP_BINARY=1
export FOX_USE_BUSYBOX_BINARY=1
export FOX_USE_BASH_SHELL=1
export FOX_BASH_TO_SYSTEM_BIN=1
export FOX_ASH_IS_BASH=1
export FOX_REPLACE_TOOLBOX_GETPROP=1
export FOX_DELETE_AROMAFM=1

# Prebuilt kernel
export OF_FORCE_PREBUILT_KERNEL=1

# Maintainer & Build Info
export FOX_MAINTAINER_PATCH_VERSION="1"
export OF_MAINTAINER="Axlorin"

else
   if [ -z "$FOX_BUILD_DEVICE" -a -z "$BASH_SOURCE" ]; then
      echo "I: This script requires bash. Not processing the $FDEVICE $(basename $0)"
   fi
fi
