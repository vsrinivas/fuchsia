// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_WLAN_PROTOCOL_IOCTL_H
#define GARNET_LIB_WLAN_PROTOCOL_IOCTL_H

#include <zircon/compiler.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// wlanphy ioctls

// Gets a channel for communicating with a wlanphy device.
#define IOCTL_WLANPHY_CONNECT IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_WLANPHY, 0)

IOCTL_WRAPPER_IN(ioctl_wlanphy_connect, IOCTL_WLANPHY_CONNECT, zx_handle_t);

// wlanif ioctls

// Gets a channel for communicating with a wlanif device.
#define IOCTL_WLAN_GET_CHANNEL IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_WLAN, 0)

__END_CDECLS

#endif  // GARNET_LIB_WLAN_PROTOCOL_IOCTL_H
