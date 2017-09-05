// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <magenta/device/usb.h>
#include <magenta/hw/usb.h>
#include <sync/completion.h>
#include <threads.h>

// Represents an interface within a composite device
typedef struct {
    mx_device_t* mxdev;
    usb_device_t* device;
    mx_device_t* hci_mxdev;
    usb_hci_protocol_t hci;
    uint32_t device_id;

    // ID of the last interface in the descriptor list.
    uint8_t last_interface_id;
    usb_descriptor_header_t* descriptor;
    size_t descriptor_length;
    // descriptors for currently active endpoints
    usb_endpoint_descriptor_t* active_endpoints[USB_MAX_EPS];

    // node for our USB device's "children" list
    list_node_t node;

    // thread for calling client's iotxn complete callback
    thrd_t callback_thread;
    bool callback_thread_stop;
    // completion used for signalling callback_thread
    completion_t callback_thread_completion;
    // list of txns that need to have client's completion callback called
    list_node_t completed_txns;
    // mutex that protects the callback_* members above
    mtx_t callback_lock;

} usb_interface_t;

// for determining index into active_endpoints[]
// bEndpointAddress has 4 lower order bits, plus high bit to signify direction
// shift high bit to bit 4 so index is in range 0 - 31.
#define get_usb_endpoint_index(ep) (((ep)->bEndpointAddress & 0x0F) | ((ep)->bEndpointAddress >> 3))

typedef struct usb_device usb_device_t;

mx_status_t usb_device_add_interface(usb_device_t* device,
                                     usb_device_descriptor_t* device_descriptor,
                                     usb_interface_descriptor_t* interface_desc,
                                     size_t interface_desc_length);

mx_status_t usb_device_add_interface_association(usb_device_t* device,
                                                 usb_device_descriptor_t* device_desc,
                                                 usb_interface_assoc_descriptor_t* assoc_desc,
                                                 size_t assoc_desc_length);

void usb_device_remove_interfaces(usb_device_t* device);

// returns whether the interface with the given id was removed.
bool usb_device_remove_interface_by_id_locked(usb_device_t* device, uint8_t interface_id);

mx_status_t usb_interface_get_device_id(mx_device_t* device, uint32_t* id);

bool usb_interface_contains_interface(usb_interface_t* intf, uint8_t interface_id);

mx_status_t usb_interface_set_alt_setting(usb_interface_t* intf, uint8_t interface_id,
                                          uint8_t alt_setting);
