// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

typedef uint32_t usb_mode_t;
#define USB_MODE_NONE ((usb_mode_t)0)
#define USB_MODE_HOST ((usb_mode_t)1)
#define USB_MODE_PERIPHERAL ((usb_mode_t)2)
#define USB_MODE_OTG ((usb_mode_t)3)

// Google's USB Vendor ID.
#define GOOGLE_USB_VID 0x18D1

// USB Product ID for Zircon CDC Ethernet Function.
#define GOOGLE_USB_CDC_PID 0xA020

// USB Product ID for Zircon USB Mass Storage Function.
#define GOOGLE_USB_UMS_PID 0xA021

// USB Product ID for Zircon USB Function Test.
#define GOOGLE_USB_PERIPHERAL_TEST_PID 0xA022
