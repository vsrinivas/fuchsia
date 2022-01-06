// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DRIVER_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DRIVER_H_

#include <lib/ddk/device.h>
#include <zircon/compiler.h>

// Callbacks for wlan_fullmac_driver_ops
__BEGIN_CDECLS
zx_status_t wlan_fullmac_bind(void* ctx, zx_device_t* device);
__END_CDECLS

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLANIF_DRIVER_H_
