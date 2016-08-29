// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/hw/usb.h>
#include <stdbool.h>

typedef struct ethernet_protocol {
    mx_status_t (*send)(mx_device_t* device, const void* buffer, size_t length);
    // returns length received, or error
    mx_status_t (*recv)(mx_device_t* device, void* buffer, size_t length);
    mx_status_t (*get_mac_addr)(mx_device_t* device, uint8_t* out_addr);
    bool (*is_online)(mx_device_t* device);
    size_t (*get_mtu)(mx_device_t* device);
} ethernet_protocol_t;

#define ETH_MAC_SIZE 6
