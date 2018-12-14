// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/usb-old.h>
#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>

#include <threads.h>
#include <stdatomic.h>

typedef enum {
    // The interface has not been claimed and no device has been created for it.
    AVAILABLE,
    // Another interface has claimed the interface.
    CLAIMED,
    // A child device has been created for the interface.
    CHILD_DEVICE
} interface_status_t;

// Represents a USB top-level device
typedef struct {
    zx_device_t* zxdev;
    usb_protocol_t usb;
    usb_device_descriptor_t device_desc;
    usb_configuration_descriptor_t* config_desc;

    mtx_t interface_mutex;
    // Array storing whether interfaces from 0 to bNumInterfaces-1
    // are available, claimed or is a child device.
    interface_status_t* interface_statuses;
    uint8_t num_interfaces;

    // list of usb_interface_t
    list_node_t children;
} usb_composite_t;

// Marks the interface as claimed, removing the device if it exists.
// Returns an error if the interface was already claimed by another interface.
zx_status_t usb_composite_do_claim_interface(usb_composite_t* comp, uint8_t interface_id);

zx_status_t usb_composite_set_interface(usb_composite_t* comp, uint8_t interface_id,
                                        uint8_t alt_setting);
