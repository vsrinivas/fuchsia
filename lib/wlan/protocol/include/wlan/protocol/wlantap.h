// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

typedef struct wlantap_ioctl_create_wlanphy {
    // A user-supplied channel handle for interacting with the wlantap PHY device.
    // The device is automatically released if the channel is closed by the user.
    zx_handle_t channel;
    // Serialized WlantapPhyConfig FIDL struct
    uint8_t config[];
} wlantap_ioctl_create_wlanphy_t;

// Creates a new fake wlanphy device
// in: wlantap_ioctl_create_wlanphy_t
#define IOCTL_WLANTAP_CREATE_WLANPHY IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_WLANTAP, 0)

IOCTL_WRAPPER_IN(ioctl_wlantap_create_wlanphy, IOCTL_WLANTAP_CREATE_WLANPHY,
                 wlantap_ioctl_create_wlanphy_t);
