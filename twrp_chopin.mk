#
# Copyright (C) 2021 The TWRP Open Source Project
#
# Licensed under the Apache License, Version 2.0
# http://www.apache.org/licenses/LICENSE-2.0
#

# Lunch (OrangeFox fox_12.1 manifest): lunch twrp_chopin-eng

# Base configuration
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/base.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/gsi_keys.mk)

# Recovery vendor config (TWRP/OrangeFox)
$(call inherit-product, vendor/twrp/config/common.mk)

# Device-specific configuration
$(call inherit-product, device/xiaomi/chopin/device.mk)

# OrangeFox settings (ignored by plain TWRP builds)
$(call inherit-product, device/xiaomi/chopin/fox_chopin.mk)

# Device identity
PRODUCT_DEVICE := chopin
PRODUCT_NAME := twrp_chopin
PRODUCT_BRAND := POCO
PRODUCT_MODEL := 21061110AG
PRODUCT_MANUFACTURER := Xiaomi
