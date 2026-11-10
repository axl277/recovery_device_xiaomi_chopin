#
# Copyright (C) 2023 The Android Open Source Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common Omni stuff.
$(call inherit-product, vendor/twrp/config/common.mk)

# Inherit from chopin device
$(call inherit-product, device/xiaomi/chopin/device.mk)

# Inherit from fox_chopin configs
$(call inherit-product, device/xiaomi/chopin/fox_chopin.mk)

PRODUCT_DEVICE := chopin
PRODUCT_NAME := twrp_chopin
PRODUCT_BRAND := POCO
PRODUCT_MODEL := 21061110AG
PRODUCT_MANUFACTURER := Xiaomi

PRODUCT_GMS_CLIENTID_BASE := android-xiaomi
