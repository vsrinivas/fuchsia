// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

__BEGIN_CDECLS

#define IOCTL_WLAN_GET_CHANNEL \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_WLAN, 0)

// ssize_t ioctl_wlan_get_channel(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_wlan_get_channel, IOCTL_WLAN_GET_CHANNEL, zx_handle_t);

__END_CDECLS
