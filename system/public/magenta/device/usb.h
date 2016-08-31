// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>

__BEGIN_CDECLS

// Device type for top-level USB device
#define USB_DEVICE_TYPE_DEVICE          1
// Device type for an interface in a USB composite device
#define USB_DEVICE_TYPE_INTERFACE       2

// returns the device type (either USB_DEVICE_TYPE_DEVICE or USB_DEVICE_TYPE_INTERFACE)
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DEVICE_TYPE       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 0)

// returns the speed of the USB device as a usb_speed_t value
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DEVICE_SPEED      IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 1)

// returns the device's USB device descriptor
// call with out_len = sizeof(usb_device_descriptor_t)
#define IOCTL_USB_GET_DEVICE_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 2)

// returns the size of the USB configuration descriptor for the device's current configuration
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_CONFIG_DESC_SIZE  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 3)

// returns the USB configuration descriptor for the device's current configuration
// call with out_len = value returned from IOCTL_USB_GET_CONFIG_DESC_SIZE
#define IOCTL_USB_GET_CONFIG_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 4)

// returns the size of the USB descriptors returned by IOCTL_USB_GET_DESCRIPTORS
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DESCRIPTORS_SIZE  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 5)

// returns the USB descriptors for an abstract USB device
// for top-level USB devices, this begins with USB configuration descriptor for the active configuration
// followed by the remaining descriptors for the configuration
// for children of USB composite devices, this begins with the USB interface descriptor
// for the interface, followed by descriptors associated with that interface
// call with out_len = value returned from IOCTL_USB_GET_DESCRIPTORS_SIZE
#define IOCTL_USB_GET_DESCRIPTORS       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 6)

// fetches a string descriptor from the USB device
// string index is passed via in_buf
// call with in_len = sizeof(int) and out_len = size of buffer to receive string (256 recommended)
#define IOCTL_USB_GET_STRING_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 7)

__END_CDECLS
