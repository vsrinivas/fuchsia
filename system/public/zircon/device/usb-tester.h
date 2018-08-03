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

// ssize_t ioctl_usb_tester_bulk_loopback(int fd, const usb_tester_params_t* buf);
#define IOCTL_USB_TESTER_BULK_LOOPBACK IOCTL(IOCTL_KIND_DEFAULT, IOCTL_FAMILY_USB_TEST, 0)
IOCTL_WRAPPER_IN(ioctl_usb_tester_bulk_loopback,
                 IOCTL_USB_TESTER_BULK_LOOPBACK, usb_tester_params_t);
