// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

// The following are not real devices, but are examples created to test
// functionality

// Adapted from boot_mouse_r_desc, this tests the push and pop ability
extern "C" const uint8_t push_pop_test[62] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)

    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x03,        //     Usage Maximum (0x03)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)
    0xA4,              //     Push

    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position)
    0xA4,              //     Push

    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xA4,              //     Push

    0xB4,              //     Pop
    0x81, 0x06,        //     Input (Data,Var,Rel,No Wrap,Linear,No Null Position)
    0xB4,              //     Pop
    0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,No Null Position)
    0xB4,              //     Pop
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,No Null Position)

    0xC0,              //   End Collection
    0xC0,              // End Collection
};

extern "C" const uint8_t minmax_signed_test[68] = {
    0x05, 0x0D,  // Usage Page (Digitizer)
    0x09, 0x04,  // Usage (Touch Screen)
    0xA1, 0x01,  // Collection (Application)

    0x05, 0x0D,  //     Usage Page (Digitizer)
    0x75, 0x10,  //     Report Size (16)
    0x95, 0x01,  //     Report Count (1)
    0x09, 0x51,  //     Usage (0x51)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0xFF,  //     Logical Maximum (-1)
    0x35, 0x00,        //     Physical Minimum (0)
    0x46, 0xFF, 0xFF,  //     Physical Maximum (65535)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No

    0x05, 0x0D,  //     Usage Page (Digitizer)
    0x75, 0x10,  //     Report Size (16)
    0x95, 0x01,  //     Report Count (1)
    0x09, 0x51,  //     Usage (0x51)
    0x15, 0xFB,        //     Logical Minimum (-5)
    0x25, 0xFF,  //     Logical Maximum (-1)
    0x35, 0xFB,       //     Physical Minimum (-5)
    0x46, 0xFF, 0xFF,  //     Physical Maximum (-1)
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No

    0x05, 0x0D,  //     Usage Page (Digitizer)
    0x75, 0x10,  //     Report Size (16)
    0x95, 0x01,  //     Report Count (1)
    0x09, 0x51,  //     Usage (0x51)
    0x15, 0xFB,        //     Logical Minimum (-5)
    0x25, 0x5,  //     Logical Maximum (5)
    0x35, 0xFB,       //     Physical Minimum (-5)
    0x46, 0x00, 0x05,  //     Physical Maximum (5)
    0x05, 0x01,  //     Usage Page (Generic Desktop Ctrls)
    0x81, 0x02,  //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No

    0xC0,        // End Collection
};
