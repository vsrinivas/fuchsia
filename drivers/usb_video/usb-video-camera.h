// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_USB_VIDEO_USB_VIDEO_CAMERA_H_
#define GARNET_DRIVERS_USB_VIDEO_USB_VIDEO_CAMERA_H_

#include <zircon/device/ioctl-wrapper.h>
#include <zircon/device/ioctl.h>

#include <fuchsia/camera/driver/cpp/fidl.h>

#define CAMERA_IOCTL_GET_CHANNEL \
  IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_CAMERA, 0)
IOCTL_WRAPPER_OUT(ioctl_camera_get_channel, CAMERA_IOCTL_GET_CHANNEL,
                  zx_handle_t);

#endif  // GARNET_DRIVERS_USB_VIDEO_USB_VIDEO_CAMERA_H_
