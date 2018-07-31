// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// Loads the firmware onto the USB test device, and renumerates as the new device.
//  in: zx_handle_t (vmo)
//  out: none
#define IOCTL_USB_TEST_FWLOADER_LOAD_FIRMWARE \
    IOCTL(IOCTL_KIND_SET_HANDLE, IOCTL_FAMILY_USB_TEST, 1)

// ssize_t ioctl_usb_test_fwloader_load_firmware(int fd, zx_handle_t* firmware_vmo);
IOCTL_WRAPPER_IN(ioctl_usb_test_fwloader_load_firmware,
                 IOCTL_USB_TEST_FWLOADER_LOAD_FIRMWARE, zx_handle_t);
