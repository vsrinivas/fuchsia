// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/protocol/usb-hci.h>

typedef struct usb_device usb_device_t;

// Represents a USB bus, which manages all devices for a USB host controller
typedef struct usb_bus {
    zx_device_t* zxdev;
    zx_device_t* hci_zxdev;
    usb_hci_protocol_t hci;
    zx_handle_t bti_handle; // handle is shared from HCI layer

    // top-level USB devices, indexed by device_id
    usb_device_t** devices;
    size_t max_device_count;
} usb_bus_t;
