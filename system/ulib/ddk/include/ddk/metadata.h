// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <zircon/boot/image.h>

// This file contains metadata types for device_get_metadata()

// MAC Address for Ethernet, Wifi, Bluetooth, etc.
// Content: uint8_t[] (variable length based on type of MAC address)
#define DEVICE_METADATA_MAC_ADDRESS               0x43414D6D // mMAC
static_assert(DEVICE_METADATA_MAC_ADDRESS == ZBI_TYPE_DRV_MAC_ADDRESS, "");

// Partition map for raw block device.
// Content: bootdata_partition_map_t
#define DEVICE_METADATA_PARTITION_MAP             0x5452506D // mPRT
static_assert(DEVICE_METADATA_PARTITION_MAP == ZBI_TYPE_DRV_PARTITION_MAP, "");

// maximum size of DEVICE_METADATA_PARTITION_MAP data
#define METADATA_PARTITION_MAP_MAX                4096

// Initial USB mode
// type: usb_mode_t
#define DEVICE_METADATA_USB_MODE                  0x4D425355 // USBM

// Serial port info
// type: serial_port_info_t
#define DEVICE_METADATA_SERIAL_PORT_INFO          0x4D524553 // SERM

// Platform board name (for sysinfo driver)
// type: char[ZBI_BOARD_NAME_LEN]
#define DEVICE_METADATA_BOARD_NAME                0x4E524F42 // BORN

// Interrupt controller type (for sysinfo driver)
// type: uint8_t
#define DEVICE_METADATA_INTERRUPT_CONTROLLER_TYPE 0x43544E49 // INTC

// GUID map (for GPT driver)
// type: array of guid_map_t
#define DEVICE_METADATA_GUID_MAP                  0x44495547 // GUID
#define DEVICE_METADATA_GUID_MAP_MAX_ENTRIES      8

// Metadata types that have least significant byte set to lowercase 'd'
// signify private driver data.
// This allows creating metadata types to be defined local to a particular
// driver or driver protocol.
#define DEVICE_METADATA_PRIVATE             0x00000064
#define DEVICE_METADATA_PRIVATE_MASK        0x000000ff

static inline bool is_private_metadata(uint32_t type) {
    return ((type & DEVICE_METADATA_PRIVATE_MASK) == DEVICE_METADATA_PRIVATE);
}
