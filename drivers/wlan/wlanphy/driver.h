// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>

// Retrieves the async_t* for this driver.
//
// This pointer is guaranteed to be valid after the driver .init hook returns and before the driver
// .release hook is called. Therefore any device created and bound by this driver may assume the
// async_t* is initialized and running.
async_t* wlanphy_async_t();

// Callbacks for wlanphy_driver_ops
__BEGIN_CDECLS
zx_status_t wlanphy_init(void** out_ctx);
zx_status_t wlanphy_bind(void* ctx, zx_device_t* device);
__END_CDECLS
