// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>

__BEGIN_CDECLS;

// protocol data for iotxns
typedef struct usb_protocol_data {
    usb_setup_t setup;      // for control transactions
    uint64_t frame;         // frame number for scheduling isochronous transfers
    uint32_t device_id;
    uint8_t ep_address;     // bEndpointAddress from endpoint descriptor
} usb_protocol_data_t;

typedef struct usb_protocol_ops {
    mx_status_t (*reset_endpoint)(void* ctx, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint8_t ep_address);
    uint32_t (*get_device_id)(void* ctx);
} usb_protocol_ops_t;

typedef struct usb_protocol {
    usb_protocol_ops_t* ops;
    void* ctx;
} usb_protocol_t;

__END_CDECLS;
