// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>

__BEGIN_CDECLS

// This ioctl can be used on a bt-host device to open a Host FIDL
// interface channel (see garnet/lib/bluetooth/fidl/host.fidl).
#define IOCTL_BT_HOST_OPEN_CHANNEL \
  IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_BT_HOST, 0)

// ssize_t ioctl_bt_host_open_channel(int fd, zx_handle_t* out);
IOCTL_WRAPPER_OUT(ioctl_bt_host_open_channel, IOCTL_BT_HOST_OPEN_CHANNEL,
                  zx_handle_t);

__END_CDECLS
