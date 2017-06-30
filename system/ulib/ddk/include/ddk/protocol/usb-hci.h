// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <magenta/compiler.h>
#include <magenta/hw/usb.h>
#include <magenta/hw/usb-hub.h>
#include <stdbool.h>

__BEGIN_CDECLS;

typedef struct usb_hci_protocol_ops {
    void (*set_bus_device)(void* ctx, mx_device_t* busdev);
    size_t (*get_max_device_count)(void* ctx);
    // enables or disables an endpoint using parameters derived from ep_desc
    mx_status_t (*enable_endpoint)(void* ctx, uint32_t device_id,
                                   usb_endpoint_descriptor_t* ep_desc,
                                   usb_ss_ep_comp_descriptor_t* ss_comp_desc, bool enable);

    // returns the current frame (in milliseconds), used for isochronous transfers
    uint64_t (*get_current_frame)(void* ctx);

    // Hub support
    mx_status_t (*configure_hub)(void* ctx, uint32_t device_id, usb_speed_t speed,
                 usb_hub_descriptor_t* descriptor);
    mx_status_t (*hub_device_added)(void* ctx, uint32_t device_id, int port, usb_speed_t speed);
    mx_status_t (*hub_device_removed)(void* ctx, uint32_t device_id, int port);
    mx_status_t (*reset_endpoint)(void* ctx, uint32_t device_id, uint8_t ep_address);
    size_t (*get_max_transfer_size)(void* ctx, uint32_t device_id, uint8_t ep_address);
} usb_hci_protocol_ops_t;

typedef struct usb_hci_protocol {
    usb_hci_protocol_ops_t* ops;
    void* ctx;
} usb_hci_protocol_t;

__END_CDECLS;
