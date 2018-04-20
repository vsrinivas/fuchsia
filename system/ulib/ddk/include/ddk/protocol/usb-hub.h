// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/driver.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// interface for use by the usb-bus to talk to the hub driver
typedef struct {
     void (*reset_port)(void* ctx, uint32_t port);
} usb_hub_interface_ops_t;

typedef struct usb_hub_interface {
    usb_hub_interface_ops_t* ops;
    void* ctx;
} usb_hub_interface_t;

static inline void usb_hub_reset_port(usb_hub_interface_t* hub, uint32_t port) {
    hub->ops->reset_port(hub->ctx, port);
}
__END_CDECLS;
