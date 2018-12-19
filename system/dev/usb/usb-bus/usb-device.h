// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/protocol/usb/hci.h>
#include <ddk/protocol/usb/hub.h>
#include <usb/usb-request.h>
#include <lib/sync/completion.h>
#include <zircon/hw/usb.h>

#include <threads.h>
#include <stdatomic.h>

typedef struct usb_bus usb_bus_t;

// Represents a USB top-level device
typedef struct usb_device {
    zx_device_t* zxdev;
    zx_device_t* hci_zxdev;
    usb_hci_protocol_t hci;
    usb_bus_t* bus;

    // ID assigned by host controller
    uint32_t device_id;
    // device_id of the hub we are attached to (or zero for root hub)
    uint32_t hub_id;
    usb_speed_t speed;

    // Interface to talk to the hub driver
    usb_hub_interface_t hub_intf;

    usb_device_descriptor_t device_desc;
    usb_configuration_descriptor_t** config_descs;
    uint8_t current_config_index;
    uint8_t num_configurations;

    atomic_bool langids_fetched;
    atomic_uintptr_t lang_ids;

    // thread for calling client's usb request complete callback
    thrd_t callback_thread;
    bool callback_thread_stop;
    // completion used for signalling callback_thread
    sync_completion_t callback_thread_completion;
    // list of requests that need to have client's completion callback called
    list_node_t completed_reqs;
    // mutex that protects the callback_* members above
    mtx_t callback_lock;

    // pool of requests that can be reused
    usb_request_pool_t free_reqs;
    size_t parent_req_size;
    size_t req_size;
} usb_device_t;

void usb_device_set_hub_interface(usb_device_t* dev, const usb_hub_interface_t* hub_intf);

zx_status_t usb_device_add(usb_bus_t* bus, uint32_t device_id, uint32_t hub_id,
                           usb_speed_t speed, usb_device_t** out_device);

#define USB_REQ_TO_DEV_INTERNAL(req, size) \
    ((usb_device_req_internal_t *)((uintptr_t)(req) + (size)))
#define DEV_INTERNAL_TO_USB_REQ(ctx, size) ((usb_request_t *)((uintptr_t)(ctx) - (size)))
