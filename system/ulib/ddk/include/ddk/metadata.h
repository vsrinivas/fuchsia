// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/boot/bootdata.h>

// This file contains metadata types for device_get_metadata()

// MAC Address for Ethernet, Wifi, Bluetooth, etc.
// Content: uint8_t[] (variable length based on type of MAC address)
#define DEVICE_METADATA_MAC_ADDRESS     BOOTDATA_MAC_ADDRESS

// Partition map for raw block device.
// Content: bootdata_partition_map_t
#define DEVICE_METADATA_PARTITION_MAP   BOOTDATA_PARTITION_MAP

// maximum size of DEVICE_METADATA_PARTITION_MAP data
#define METADATA_PARTITION_MAP_MAX 4096

