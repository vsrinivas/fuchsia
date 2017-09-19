// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/iotxn.h>
#include <ddk/protocol/usb-mode-switch.h>
#include <zircon/types.h>
#include <zircon/hw/usb.h>
#include <sync/completion.h>
#include <threads.h>

typedef struct usb_virtual_host usb_virtual_host_t;
typedef struct usb_virtual_device usb_virtual_device_t;

typedef struct {
    list_node_t host_txns;
    list_node_t device_txns;
    // offset into current host txn, for dealing with host txns that are bigger than
    // their matching device txn
    zx_off_t txn_offset;
    bool stalled;
} usb_virtual_ep_t;

typedef struct {
    zx_device_t* zxdev;
    usb_virtual_host_t* host;
    usb_virtual_device_t* device;

    usb_virtual_ep_t eps[USB_MAX_EPS];

    mtx_t lock;
    completion_t completion;
    usb_mode_t usb_mode;
    bool connected;
} usb_virtual_bus_t;

zx_status_t usb_virtual_bus_set_mode(usb_virtual_bus_t* bus, usb_mode_t mode);
zx_status_t usb_virtual_bus_set_stall(usb_virtual_bus_t* bus, uint8_t ep_address, bool stall);

zx_status_t usb_virtual_host_add(usb_virtual_bus_t* bus, usb_virtual_host_t** out_host);
void usb_virtual_host_release(usb_virtual_host_t* host);
void usb_virtual_host_set_connected(usb_virtual_host_t* host, bool connected);

zx_status_t usb_virtual_device_add(usb_virtual_bus_t* bus, usb_virtual_device_t** out_device);
void usb_virtual_device_release(usb_virtual_device_t* host);
void usb_virtual_device_control(usb_virtual_device_t* device, iotxn_t* txn);
