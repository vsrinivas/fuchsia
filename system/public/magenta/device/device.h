// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>

// Bind to a driver
//   in: driver to bind to (optional)
//   out: none
#define IOCTL_DEVICE_BIND \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 0)

// Watch a directory for changes
//   in: none
//   out: handle to msgpipe to get notified on
#define IOCTL_DEVICE_WATCH_DIR \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 1)

// Return a handle to the device event
//   in: none
//   out: handle
#define IOCTL_DEVICE_GET_EVENT_HANDLE \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_DEVICE, 2)

// Return driver name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DRIVER_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 3)

// Return device name string
//   in: none
//   out: null-terminated string
#define IOCTL_DEVICE_GET_DEVICE_NAME \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 4)

// Indicates if there's data available to read,
// or room to write, or an error condition.
#define DEVICE_SIGNAL_READABLE MX_SIGNAL_SIGNAL0
#define DEVICE_SIGNAL_WRITABLE MX_SIGNAL_SIGNAL1
#define DEVICE_SIGNAL_ERROR MX_SIGNAL_SIGNAL2
