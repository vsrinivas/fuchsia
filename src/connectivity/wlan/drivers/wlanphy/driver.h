// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DRIVER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DRIVER_H_

#include <lib/async/dispatcher.h>
#include <zircon/compiler.h>

#include <ddk/device.h>

// Retrieves the async_t* for this driver.
//
// This pointer is guaranteed to be valid after the driver .init hook returns and before the driver
// .release hook is called. Therefore any device created and bound by this driver may assume the
// async_dispatcher_t* is initialized and running.
async_dispatcher_t* wlanphy_async_t();

// Callbacks for wlanphy_driver_ops
__BEGIN_CDECLS
zx_status_t wlanphy_init(void** out_ctx);
zx_status_t wlanphy_bind(void* ctx, zx_device_t* device);
__END_CDECLS

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANPHY_DRIVER_H_
