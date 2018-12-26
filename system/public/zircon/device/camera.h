// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/hardware/camera/c/fidl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>
#include <zircon/types.h>

#define MAX_SUPPORTED_MODES 3

#define CAMERA_IOCTL_GET_SUPPORTED_MODES \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_CAMERA, 0)

// ssize_t ioctl_camera_get_supported_modes(int fd, fuchsia_hardware_camera_SensorMode* modes,
//                                          size_t out_len);
IOCTL_WRAPPER_VAROUT(ioctl_camera_get_supported_modes, CAMERA_IOCTL_GET_SUPPORTED_MODES,
                     fuchsia_hardware_camera_SensorMode);
