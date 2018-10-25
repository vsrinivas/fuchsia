// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/driver.h>
#include <ddk/protocol/usb.h>

#include "usb-tester-hw.h"

extern zx_status_t usb_tester_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t usb_tester_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = usb_tester_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(usb_tester, usb_tester_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, GOOGLE_VID),
    BI_MATCH_IF(EQ, BIND_USB_PID, USB_TESTER_PID),
ZIRCON_DRIVER_END(usb_tester)
// clang-format on
