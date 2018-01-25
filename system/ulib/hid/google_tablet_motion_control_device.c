// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <hid/google_tablet_motion_control_device.h>
#include <zircon/errors.h>
#include <stdio.h>
#include <string.h>


// We encode the tablet mode switch events as a vendor-defined System Control.
// This is a bit hacky, but there is no tablet mode switch usage switch defined
// that we can find.  System Control collections are meant to be consumed by the
// operating system, not user applications.
static const uint8_t google_table_motion_control_device_report_desc[] = {
  0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
  0x09, 0x80,        // Usage (Sys Control)
  0xA1, 0x01,        // Collection (Application)
  0x0B, 0x01, 0x00, 0x00, 0xFF,  //   Usage (0x0-FFFFFF) [Vendor Defined]
  0x15, 0x00,        //   Logical Minimum (0)
  0x25, 0x01,        //   Logical Maximum (1)
  0x75, 0x01,        //   Report Size (1)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  0x75, 0x07,        //   Report Size (7)
  0x95, 0x01,        //   Report Count (1)
  0x81, 0x03,        //   Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
  0xC0,              // End Collection
};


bool is_google_tablet_motion_control_device_report_desc(const uint8_t* data, size_t len) {
    if (!data)
        return false;

    if (len != sizeof(google_table_motion_control_device_report_desc)) {
        return false;
    }

    return (memcmp(data, google_table_motion_control_device_report_desc, len) == 0);
}

zx_status_t setup_google_tablet_motion_control_device(int fd) {
    if (fd < 0)
        return ZX_ERR_INVALID_ARGS;

    return ZX_OK;
}
