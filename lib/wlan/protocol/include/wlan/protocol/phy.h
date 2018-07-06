// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <wlan/protocol/info.h>
#include <zircon/compiler.h>

__BEGIN_CDECLS

typedef struct wlanphy_info {
    // Phy wlan capabilities information
    wlan_info_t wlan_info;
} wlanphy_info_t;

typedef struct wlanphy_protocol_ops {
    uint32_t reserved;
} wlanphy_protocol_ops_t;

typedef struct wlanphy_protocol {
    wlanphy_protocol_ops_t* ops;
    void* ctx;
} wlanphy_protocol_t;

__END_CDECLS
