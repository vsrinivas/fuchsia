// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdbool.h>
#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/hw/usb.h>

enum {
    USB_MODE_NONE,
    USB_MODE_HOST,
    USB_MODE_DEVICE,
    USB_MODE_OTG,
};
typedef uint32_t usb_mode_t;

typedef struct {
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
} usb_function_descriptor_t;

// Sets the device's USB device descriptor
#define IOCTL_USB_DEVICE_SET_DEVICE_DESC    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 0)

// Sets a string descriptor a string in the USB device descriptor
#define IOCTL_USB_DEVICE_ALLOC_STRING_DESC  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 1)

// Adds a new function to the USB current configuration
// Must be called before IOCTL_USB_DEVICE_BIND_FUNCTIONS or after IOCTL_USB_DEVICE_CLEAR_FUNCTIONS
#define IOCTL_USB_DEVICE_ADD_FUNCTION       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 2)

// Tells the device to create child devices for the configuration's interfaces
#define IOCTL_USB_DEVICE_BIND_FUNCTIONS     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 3)

// Tells the device to remove the child devices for the configuration's interfaces
// and reset the list of functions to empty
#define IOCTL_USB_DEVICE_CLEAR_FUNCTIONS    IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 4)

#define IOCTL_USB_DEVICE_GET_MODE           IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 5)
#define IOCTL_USB_DEVICE_SET_MODE           IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 6)

IOCTL_WRAPPER_IN(ioctl_usb_device_set_device_desc, IOCTL_USB_DEVICE_SET_DEVICE_DESC, \
                 usb_device_descriptor_t)
IOCTL_WRAPPER_VARIN_OUT(ioctl_usb_device_alloc_string_desc, IOCTL_USB_DEVICE_ALLOC_STRING_DESC, \
                        char, uint8_t)
IOCTL_WRAPPER_IN(ioctl_usb_device_add_function, IOCTL_USB_DEVICE_ADD_FUNCTION, \
                 usb_function_descriptor_t)
IOCTL_WRAPPER(ioctl_usb_device_bind_functions, IOCTL_USB_DEVICE_BIND_FUNCTIONS)
IOCTL_WRAPPER(ioctl_usb_device_clear_functions, IOCTL_USB_DEVICE_CLEAR_FUNCTIONS)
IOCTL_WRAPPER_OUT(ioctl_usb_device_get_mode, IOCTL_USB_DEVICE_GET_MODE, usb_mode_t)
IOCTL_WRAPPER_IN(ioctl_usb_device_set_mode, IOCTL_USB_DEVICE_SET_MODE, usb_mode_t)
