// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/device/ioctl.h>
#include <zircon/device/ioctl-wrapper.h>
#include <zircon/types.h>

// Returns a socket that can be used to send input reports and receive output reports.
#define IOCTL_HIDCTL_CONFIG \
    IOCTL(IOCTL_KIND_GET_HANDLE, IOCTL_FAMILY_HID, 0)

typedef struct hid_ioctl_config {
    uint8_t dev_num;
    bool boot_device;
    uint8_t dev_class;
    size_t rpt_desc_len;
    uint8_t rpt_desc[];
} hid_ioctl_config_t;

// ssize_t ioctl_hidctl_config(int fd, const hid_ioctl_config_t* in, size_t in_len, zx_handle_t* out);
IOCTL_WRAPPER_VARIN_OUT(ioctl_hidctl_config, IOCTL_HIDCTL_CONFIG, hid_ioctl_config_t, zx_handle_t);
