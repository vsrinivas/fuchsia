// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/binding.h>

extern zx_driver_ops_t DRIVER_OPS_MICROORB;

ZIRCON_DRIVER_BEGIN(_driver_microorb, DRIVER_OPS_MICROORB, "microorb", "0.1.0", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_USB),
    BI_ABORT_IF(NE, BIND_USB_VID, 0x6666),
    BI_MATCH_IF(EQ, BIND_USB_PID, 0xf00d),
ZIRCON_DRIVER_END(_driver_microorb)
