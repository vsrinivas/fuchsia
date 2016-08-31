// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <magenta/device/ioctl.h>

// returns the speed of the USB device as a usb_speed_t value
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DEVICE_SPEED      IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 0)

// returns the device's USB device descriptor
// call with out_len = sizeof(usb_device_descriptor_t)
#define IOCTL_USB_GET_DEVICE_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 1)

// returns the size of the USB configuration descriptor for the device's current configuration
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_CONFIG_DESC_SIZE  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 2)

// returns the USB configuration descriptor for the device's current configuration
// call with out_len = value returned from IOCTL_USB_GET_CONFIG_DESC_SIZE
#define IOCTL_USB_GET_CONFIG_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 3)

// fetches a string descriptor from the USB device
// string index is passed via in_buf
// call with in_len = sizeof(int) and out_len = size of buffer to receive string (256 recommended)
#define IOCTL_USB_GET_STRING_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 4)
