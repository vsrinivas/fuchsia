// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>

// define the ioctl operation:
#define IOCTL_DEV_NUMBER_RESET \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_DEVICE, 99)

// create a client wrapper function ioctl_number_reset_value():
IOCTL_WRAPPER_IN(ioctl_number_reset_value, IOCTL_DEV_NUMBER_RESET, int)

