// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

enum {
    USB_TESTER_DATA_PATTERN_CONSTANT = 1,
    USB_TESTER_DATA_PATTERN_RANDOM   = 2,
};

typedef struct usb_tester_params {
    uint32_t data_pattern;  // USB_TESTER_DATA_PATTERN_CONSTANT or USB_TESTER_DATA_PATTERN_RANDOM.
    size_t len;             // Total number of bytes to transfer.
} usb_tester_params_t;

// ssize_t ioctl_usb_tester_set_mode_fwloader(int fd);
#define IOCTL_USB_TESTER_SET_MODE_FWLOADER IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_TEST, 0)
IOCTL_WRAPPER(ioctl_usb_tester_set_mode_fwloader, IOCTL_USB_TESTER_SET_MODE_FWLOADER);

// ssize_t ioctl_usb_tester_bulk_loopback(int fd, const usb_tester_params_t* params);
#define IOCTL_USB_TESTER_BULK_LOOPBACK IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_TEST, 1)
IOCTL_WRAPPER_IN(ioctl_usb_tester_bulk_loopback,
                 IOCTL_USB_TESTER_BULK_LOOPBACK, usb_tester_params_t);

typedef struct {
    // Number of packets loopbacked successfully.
    size_t num_passed;
    // Number of packets transferred to each EP.
    size_t num_packets;
} usb_tester_result_t;

// ssize_t ioctl_usb_tester_isoch_loopback(int fd, const usb_tester_params_t* params,
//                                         usb_tester_result_t* result);
#define IOCTL_USB_TESTER_ISOCH_LOOPBACK IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_TEST, 2)
IOCTL_WRAPPER_INOUT(ioctl_usb_tester_isoch_loopback, IOCTL_USB_TESTER_ISOCH_LOOPBACK,
                    usb_tester_params_t, usb_tester_result_t);
