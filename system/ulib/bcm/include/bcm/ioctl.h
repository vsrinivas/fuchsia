// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>
#include <magenta/compiler.h>
#include <stddef.h>

__BEGIN_CDECLS;

// Broadcomm vendor id
#define PDEV_VID_BROADCOMM  0x00BC

// Broadcomm specific PIDs and DIDs
#define PDEV_PID_BROADCOMM_RPI3         0x0001  // Raspberry PI 3

#define PDEV_DID_BROADCOMM_EMMC         0x0001  // Bcm28xx eMMC device.
#define PDEV_DID_BROADCOMM_I2C          0x0002  // Bcm28xx I2C device.
#define PDEV_DID_BROADCOMM_PCM          0x0003  // Bcm28xx PCM/I2S device.
#define PDEV_DID_BROADCOMM_USB          0x0004  // Bcm28xx USB
#define PDEV_DID_BROADCOMM_DISPLAY      0x0005  // Bcm28xx Display

#define IOCTL_BCM_POWER_ON_USB \
    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_BCM, 0)

// ssize_t ioctl_bcm_power_on_usb(int fd);
IOCTL_WRAPPER(ioctl_bcm_power_on_usb, IOCTL_BCM_POWER_ON_USB);

__END_CDECLS
