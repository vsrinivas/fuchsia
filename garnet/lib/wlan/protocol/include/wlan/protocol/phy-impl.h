// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_PHY_IMPL_H_
#define GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_PHY_IMPL_H_

#include <wlan/protocol/phy.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct wlanphy_impl_protocol_ops {
    // Get information about the capabilities of the physical device
    zx_status_t (*query)(void* ctx, wlanphy_info_t* info);

    // Create a new interface with the specified role, returning the interface id.
    // Some common error codes are:
    // ZX_ERR_NO_RESOURCES: maximum number of interfaces have already been created
    // ZX_ERR_NOT_SUPPORTED: device does not support the specified role
    zx_status_t (*create_iface)(void* ctx, wlanphy_create_iface_req_t req, uint16_t* out_iface_id);

    // Destroy the interface with the matching id.
    zx_status_t (*destroy_iface)(void* ctx, uint16_t id);

} wlanphy_impl_protocol_ops_t;

typedef struct wlanphy_impl_protocol {
    wlanphy_impl_protocol_ops_t* ops;
    void* ctx;
} wlanphy_impl_protocol_t;

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_INCLUDE_WLAN_PROTOCOL_PHY_IMPL_H_
