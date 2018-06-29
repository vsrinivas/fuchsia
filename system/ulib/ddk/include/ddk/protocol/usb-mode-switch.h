// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/device/usb-device.h>

__BEGIN_CDECLS;

typedef struct {
    zx_status_t (*set_mode)(void* ctx, usb_mode_t mode);
} usb_mode_switch_protocol_ops_t;

typedef struct {
    usb_mode_switch_protocol_ops_t* ops;
    void* ctx;
} usb_mode_switch_protocol_t;

static inline zx_status_t usb_mode_switch_set_mode(usb_mode_switch_protocol_t* ums,
                                                   usb_mode_t mode) {
    return ums->ops->set_mode(ums->ctx, mode);
}

__END_CDECLS;
