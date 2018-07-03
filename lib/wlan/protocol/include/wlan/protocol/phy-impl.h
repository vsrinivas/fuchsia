// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/protocol/common.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct wlan_iface_info {
    uint16_t id;
} wlan_iface_info_t;

typedef struct wlanphy_impl_protocol_ops {

    // Get information about the capabilities of the physical device
    zx_status_t (*query)(void* ctx, wlan_info_t* info);

    // Create a new interface with the specified role, returning the interface attributes.
    // Some common error codes are:
    // ZX_ERR_NO_RESOURCES: maximum number of interfaces have already been created
    // ZX_ERR_NOT_SUPPORTED: device does not support the specified role
    zx_status_t (*create_iface)(void* ctx,
                                uint16_t role,
                                wlan_iface_info_t* info);

    // Destroy the interface with the matching id.
    zx_status_t (*destroy_iface)(void* ctx, uint16_t id);

} wlanphy_impl_protocol_ops_t;

typedef struct wlanphy_impl_protocol {
    wlanphy_impl_protocol_ops_t* ops;
    void* ctx;
} wlanphy_impl_protocol_t;

__END_CDECLS
