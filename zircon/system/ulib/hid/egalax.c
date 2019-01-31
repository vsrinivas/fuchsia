// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <hid/egalax.h>
#include <zircon/errors.h>
#include <stdio.h>
#include <string.h>

static const uint8_t egalax_touch_report_desc[] = {
    0x05, 0x0D,       // Usage Page (Digitizer)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x02,       //   Report ID (2)
    0x09, 0x20,       //   Usage (Stylus)
    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x42,       //     Usage (Tip Switch)
    0x09, 0x32,       //     Usage (In Range)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x95, 0x02,       //     Report Count (2)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,       //     Report Count (6)
    0x75, 0x01,       //     Report Size (1)
    0x81, 0x03,       //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x05, 0x01,       //     Usage (Generic Desktop Controls)
    0x09, 0x30,       //     Usage (X)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x01,       //     Report Count (1)
    0xA4,             //     Push
    0x55, 0x0D,       //       Unit Exponent (-3)
    0x65, 0x33,       //       Unit (System: English, Length: Inch)
    0x36, 0x00, 0x00, //       Physical Minimum (0)
    0x46, 0x99, 0x28, //       Physical Maximum, (10393)
    0x16, 0x00, 0x00, //       Logical Minimum (0)
    0x26, 0xFF, 0x0F, //       Logical Maximum (4095)
    0x81, 0x02,       //       Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x31,       //       Usage (Y)
    0x16, 0x00, 0x00, //       Logical Minimum (0)
    0x26, 0xFF, 0x0F, //       Logical Maximum (4095)
    0x36, 0x00, 0x00, //       Physical Minimum (0)
    0x46, 0xAF, 0x19, //       Physical Maximum (6575)
    0x81, 0x02,       //       Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xB4,             //     Pop
    0xC0,             //   End Collection
    0xC0,             // End Collection
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x01,       // Usage (Pointer)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x01,       //   Report ID (1)
    0x09, 0x01,       //   Usage (Pointer)
    0xA1, 0x00,       //   Collection (Physical)
    0x05, 0x09,       //   Usage Page (Button)
    0x19, 0x01,       //   Usage Minimum (1)
    0x29, 0x02,       //   Usage Maximum (2)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x95, 0x02,       //   Report Count (2)
    0x75, 0x01,       //   Report Size (1)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x06,       //   Report Size (6)
    0x81, 0x01,       //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x05, 0x01,       //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //   Usage (X)
    0x09, 0x31,       //   Usage (Y)
    0x16, 0x00, 0x00, //   Logical Minimum (0)
    0x26, 0xFF, 0x0F, //   Logical Maximum (4095)
    0x36, 0x00, 0x00, //   Physical Minimum (0)
    0x46, 0xFF, 0x0F, //   Physical Maximum (4095)
    0x66, 0x00, 0x00, //   Unit (None)
    0x75, 0x10,       //   Report Size (16)
    0x95, 0x02,       //   Report Count (2)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,             //   End Collection
    0xC0,             // End Collection
    0x06, 0x00, 0xFF, // Usage Page (Vendor Defined)
    0x09, 0x01,       // Usage (Pointer)
    0xA1, 0x01,       // Collection (Application)
    0x09, 0x01,       //   Usage (Pointer)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x85, 0x03,       //   Report ID (3)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x05,       //   Report Count (5)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x06, 0x00, 0xFF, //   Usage Page (Vendor Defined)
    0x09, 0x01,       //   Usage (Pointer)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x75, 0x08,       //   Report Size (8)
    0x95, 0x05,       //   Report Count (5)
    0x91, 0x02,       //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,             // End Collection
    0x05, 0x0D,       // Usage Page (Digitizer)
    0x09, 0x04,       // Usage (Touch Screen)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x04,       //   Report ID (4)
    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x42,       //     Usage (Tip Switch)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x75, 0x01,       //     Report Size (1)
    0x95, 0x01,       //     Report Count (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x32,       //     Usage (In Range)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x51,       //     Usage (Contact Identifier)
    0x75, 0x05,       //     Report Size (5)
    0x95, 0x01,       //     Report Count (1)
    0x16, 0x00, 0x00, //     Logical Minimum (0)
    0x26, 0x10, 0x00, //     Logical Maximum (16)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x47,       //     Usage (Confidence)
    0x75, 0x01,       //     Report Size (1)
    0x95, 0x01,       //     Report Count (1)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x01,       //     Logical Maximum (1)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x05, 0x01,       //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,       //     Usage (X)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x01,       //     Report Count (1)
    0x55, 0x0D,       //     Unit Exponent (-3)
    0x65, 0x33,       //     Unit (System: English, Length: Inch)
    0x35, 0x00,       //     Physical Minimum (0)
    0x46, 0x60, 0x17, //     Physical Maximum (5984)
    0x26, 0xFF, 0x0F, //     Logical Maximum (4095)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x09, 0x31,       //     Usage (Y)
    0x75, 0x10,       //     Report Size (16)
    0x95, 0x01,       //     Report Count (1)
    0x55, 0x0D,       //     Report Exponent (-3)
    0x65, 0x33,       //     Unit (System: English, Length: Inch)
    0x35, 0x00,       //     Physical Minimum (0)
    0x46, 0x26, 0x0e, //     Physical Maximum (3622)
    0x26, 0xFF, 0x0F, //     Logical Maximum (4095)
    0x81, 0x02,       //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x05, 0x0D,       //     Usage Page (Digitizer)
    0x09, 0x55,       //     Usage (Max Contact Points)
    0x25, 0x08,       //     Logical Maximum (8)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x01,       //     Report Count (1)
    0xB1, 0x02,       //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,             //   End Collection
    0xC0,             // End Collection
    0x05, 0x0D,       // Usage Page (Digitizer)
    0x09, 0x0E,       // Usage (Configuration)
    0xA1, 0x01,       // Collection (Application)
    0x85, 0x05,       //   Report ID (5)
    0x09, 0x22,       //   Usage (Finger)
    0xA1, 0x00,       //   Collection (Physical)
    0x09, 0x52,       //     Usage (Device Mode)
    0x09, 0x53,       //     Usage (Device Index)
    0x15, 0x00,       //     Logical Minimum (0)
    0x25, 0x0A,       //     Logical Maximum (10)
    0x75, 0x08,       //     Report Size (8)
    0x95, 0x02,       //     Report Count (2)
    0xB1, 0x02,       //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,             //   End Collection
    0xC0              // End Collection
    // 322 bytes
};


bool is_egalax_touchscreen_report_desc(const uint8_t* data, size_t len) {
    if (!data)
        return false;

    if (len != sizeof(egalax_touch_report_desc)) {
        return false;
    }

    return (memcmp(data, egalax_touch_report_desc, len) == 0);
}

zx_status_t setup_egalax_touchscreen(int fd) {
    if (fd < 0)
        return ZX_ERR_INVALID_ARGS;

    return ZX_OK;
}
