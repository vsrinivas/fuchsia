// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <zircon/types.h>

// Callback for devmgr to instantiate the zxcrypt::Device when ioctl_device_bind is called on a
// previously formatted block device.
extern zx_status_t zxcrypt_device_bind(void* ctx, zx_device_t* parent);

static zx_driver_ops_t zxcrypt_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = zxcrypt_device_bind,
};

// clang-format off
ZIRCON_DRIVER_BEGIN(zxcrypt, zxcrypt_driver_ops, "zircon", "0.1", 2)
    BI_ABORT_IF_AUTOBIND,
    BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_BLOCK),
ZIRCON_DRIVER_END(zxcrypt)
