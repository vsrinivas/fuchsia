// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/boot/image.h>

// This file contains metadata types for device_get_metadata()

// MAC Address for Ethernet, Wifi, Bluetooth, etc.
// Content: uint8_t[] (variable length based on type of MAC address)
#define DEVICE_METADATA_MAC_ADDRESS     ZBI_TYPE_DRV_MAC_ADDRESS

// Partition map for raw block device.
// Content: bootdata_partition_map_t
#define DEVICE_METADATA_PARTITION_MAP   ZBI_TYPE_DRV_PARTITION_MAP

// Metadata types that have least significant byte set to lowercase 'd'
// signify driver data.  Ex: Used by board files to pass config info
// to drivers via platform_dev
#define DEVICE_METADATA_DRIVER_DATA         0x00000064
#define DEVICE_METADATA_DRIVER_DATA_MASK    0x000000ff

// maximum size of DEVICE_METADATA_PARTITION_MAP data
#define METADATA_PARTITION_MAP_MAX 4096

static inline bool is_driver_meta(uint32_t val){
    return ((val & DEVICE_METADATA_DRIVER_DATA_MASK) == DEVICE_METADATA_DRIVER_DATA);
}