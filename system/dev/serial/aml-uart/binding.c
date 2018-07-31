// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/platform-defs.h>

extern zx_status_t aml_uart_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t aml_uart_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = aml_uart_bind,
};

ZIRCON_DRIVER_BEGIN(aml_uart, aml_uart_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PLATFORM_DEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_UART),
ZIRCON_DRIVER_END(aml_uart)
