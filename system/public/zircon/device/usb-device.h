// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/hw/usb.h>

__BEGIN_CDECLS

// maximum length of a USB string after conversion to UTF8
#define MAX_USB_STRING_LEN  ((((UINT8_MAX - sizeof(usb_descriptor_header_t)) / \
                                sizeof(uint16_t)) * 3) + 1)

typedef struct usb_ioctl_get_string_desc_req {
    uint16_t lang_id;
    uint8_t desc_id;
} __PACKED usb_ioctl_get_string_desc_req_t;

typedef struct usb_ioctl_get_string_desc_resp {
    uint16_t lang_id;
    uint16_t data_len;
    uint8_t data[];
} __PACKED usb_ioctl_get_string_desc_resp_t;

// returns the speed of the USB device as a usb_speed_t value
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DEVICE_SPEED      IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 1)

// returns the device's USB device descriptor
// call with out_len = sizeof(usb_device_descriptor_t)
#define IOCTL_USB_GET_DEVICE_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 2)

// returns the size of the USB configuration descriptor for a device's configuration
// in: configuration number
// out: configuration descriptor size
#define IOCTL_USB_GET_CONFIG_DESC_SIZE  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 3)

// returns the USB configuration descriptor for a device's configuration
// in: configuration number
// out: configuration descriptor
// call with out_len = value returned from IOCTL_USB_GET_CONFIG_DESC_SIZE
#define IOCTL_USB_GET_CONFIG_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 4)

// returns the size of the USB descriptors returned by IOCTL_USB_GET_DESCRIPTORS
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_DESCRIPTORS_SIZE  IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 5)

// returns the USB descriptors for an abstract USB device
// for top-level USB devices, this begins with USB configuration descriptor for the active
// configuration followed by the remaining descriptors for the configuration
// for children of USB composite devices, this begins with USB interface descriptor
// or interface association descriptor
// for the interface, followed by descriptors associated with that interface
// call with out_len = value returned from IOCTL_USB_GET_DESCRIPTORS_SIZE
#define IOCTL_USB_GET_DESCRIPTORS       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 6)

// fetches a string descriptor from the USB device
//
// in_buf should be a usb_ioctl_get_string_desc_req_t
// ++ in_buf.lang_id : The language ID of the string descriptor to fetch.  If no
//                     matching language ID is present in the device's language
//                     ID table, the first entry of the language ID table will
//                     be substituted.
// ++ in_buf.desc_id : The ID of the string descriptor to fetch, or 0 to fetch
//                     the language table instead.
//
// out_but should be large enough to hold a usb_ioctl_get_string_desc_resp_t,
// along with the actual payload.
// ++ out_buf.lang_id  : The actual language ID of the string fetched, or 0 for
//                       the language ID table.
// ++ out_buf.data_len : The number of byte which *would be required* to hold
//                       the payload, in bytes.  Note, this value may be larger
//                       than the space for payload supplied by the user.
// ++ out_buf.data     : As much of the payload will fit within the supplied
//                       buffer.  Strings will be encoded using UTF-8, and are
//                       *not* guaranteed to be null terminated.
//
// The worst case size for the payload of a language ID table should be 252
// bytes, meaning that a 256 byte buffer should always be enough to hold any
// language ID table.
//
// The worst case size for a UTF-8 encoded string descriptor payload should be
// 378 bytes (126 UTF-16 code units with a worst case expansion factor of 3)
#define IOCTL_USB_GET_STRING_DESC       IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 7)

// selects an alternate setting for an interface on a USB device
// called with in_buf pointing to an array of two ints,
// the first being the interface number and the second the alternate setting,
// and in_len = 2 * sizeof(int)
#define IOCTL_USB_SET_INTERFACE         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 8)

// returns an implementation specific device ID for a USB device
// for informational purposes only
// call with out_len = sizeof(uint64_t)
#define IOCTL_USB_GET_DEVICE_ID         IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 10)

// returns the device ID for the hub that a USB device is connected to
// for informational purposes only
// call with out_len = sizeof(uint64_t)
#define IOCTL_USB_GET_DEVICE_HUB_ID     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 11)

// returns the device's current configuration
// call with out_len = sizeof(int)
#define IOCTL_USB_GET_CONFIGURATION     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 12)

// sets the device's current configuration
// call with in_len = sizeof(int)
#define IOCTL_USB_SET_CONFIGURATION     IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_DEVICE, 13)

IOCTL_WRAPPER_OUT(ioctl_usb_get_device_speed, IOCTL_USB_GET_DEVICE_SPEED, int);
IOCTL_WRAPPER_OUT(ioctl_usb_get_device_desc, IOCTL_USB_GET_DEVICE_DESC, usb_device_descriptor_t);
IOCTL_WRAPPER_INOUT(ioctl_usb_get_config_desc_size, IOCTL_USB_GET_CONFIG_DESC_SIZE, int, int);
IOCTL_WRAPPER_IN_VAROUT(ioctl_usb_get_config_desc, IOCTL_USB_GET_CONFIG_DESC, int, void);
IOCTL_WRAPPER_OUT(ioctl_usb_get_descriptors_size, IOCTL_USB_GET_DESCRIPTORS_SIZE, int);
IOCTL_WRAPPER_VAROUT(ioctl_usb_get_descriptors, IOCTL_USB_GET_DESCRIPTORS, void);
IOCTL_WRAPPER_IN_VAROUT(ioctl_usb_get_string_desc, IOCTL_USB_GET_STRING_DESC,
                        usb_ioctl_get_string_desc_req_t, usb_ioctl_get_string_desc_resp_t);

static inline ssize_t ioctl_usb_set_interface(int fd, int interface_number, int alt_setting) {
    int args[2] = { interface_number, alt_setting };
    return fdio_ioctl(fd, IOCTL_USB_SET_INTERFACE, args, sizeof(args), NULL, 0);
}

IOCTL_WRAPPER_OUT(ioctl_usb_get_device_id, IOCTL_USB_GET_DEVICE_ID, uint64_t);
IOCTL_WRAPPER_OUT(ioctl_usb_get_device_hub_id, IOCTL_USB_GET_DEVICE_HUB_ID, uint64_t);
IOCTL_WRAPPER_OUT(ioctl_usb_get_configuration, IOCTL_USB_GET_CONFIGURATION, int);
IOCTL_WRAPPER_IN(ioctl_usb_set_configuration, IOCTL_USB_SET_CONFIGURATION, int);

__END_CDECLS
