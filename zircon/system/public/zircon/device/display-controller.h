// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SYSROOT_ZIRCON_DEVICE_DISPLAY_CONTROLLER_H_
#define SYSROOT_ZIRCON_DEVICE_DISPLAY_CONTROLLER_H_

#include <stdint.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

#define IOCTL_DISPLAY_CONTROLLER_GET_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DISPLAY_CONTROLLER, 1)

IOCTL_WRAPPER_OUT(ioctl_display_controller_get_handle,
                  IOCTL_DISPLAY_CONTROLLER_GET_HANDLE, zx_handle_t)

#define IMAGE_TYPE_SIMPLE UINT32_C(0)
#define INVALID_ID 0

#endif  // SYSROOT_ZIRCON_DEVICE_DISPLAY_CONTROLLER_H_
