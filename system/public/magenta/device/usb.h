// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <magenta/device/ioctl.h>
#include <magenta/device/ioctl-wrapper.h>

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

// selects an alternate setting for an interface on a USB device
// called with in_buf pointing to an array of two ints,
// the first being the interface number and the second the alternate setting,
// and in_len = 2 * sizeof(int)
#define IOCTL_USB_SET_INTERFACE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 8)

// returns the current frame number for the USB controller (in milliseconds)
// call with out_len = sizeof(uint64_t)
#define IOCTL_USB_GET_CURRENT_FRAME     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB, 9)

IOCTL_WRAPPER_OUT(ioctl_usb_get_device_type, IOCTL_USB_GET_DEVICE_TYPE, int);
IOCTL_WRAPPER_OUT(ioctl_usb_get_device_speed, IOCTL_USB_GET_DEVICE_SPEED, int);
IOCTL_WRAPPER_OUT(ioctl_usb_get_device_desc, IOCTL_USB_GET_DEVICE_DESC, usb_device_descriptor_t);
IOCTL_WRAPPER_OUT(ioctl_usb_get_config_desc_size, IOCTL_USB_GET_CONFIG_DESC_SIZE, int);
IOCTL_WRAPPER_VAROUT(ioctl_usb_get_config_desc, IOCTL_USB_GET_CONFIG_DESC, void);
IOCTL_WRAPPER_OUT(ioctl_usb_get_descriptors_size, IOCTL_USB_GET_DESCRIPTORS_SIZE, int);
IOCTL_WRAPPER_VAROUT(ioctl_usb_get_descriptors, IOCTL_USB_GET_DESCRIPTORS, void);
IOCTL_WRAPPER_IN_VAROUT(ioctl_usb_get_string_desc, IOCTL_USB_GET_STRING_DESC, int, void);

static inline ssize_t ioctl_usb_set_interface(int fd, const int in[static 2]) {
    return mxio_ioctl(fd, IOCTL_USB_SET_INTERFACE, in, 2*sizeof(int), NULL, 0);
}

IOCTL_WRAPPER_OUT(ioctl_usb_get_current_frame, IOCTL_USB_GET_CURRENT_FRAME, uint64_t);

__END_CDECLS
