// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>

zx_status_t usb_device_control(usb_device_t* dev, uint8_t request_type,  uint8_t request,
                               uint16_t value, uint16_t index, void* data, size_t length);

zx_status_t usb_device_get_descriptor(usb_device_t* dev, uint16_t type,
                                      uint16_t index, uint16_t language, void* data, size_t length);

// maximum length of a USB string after conversion to UTF8
#define MAX_USB_STRING_LEN  ((((UINT8_MAX - sizeof(usb_descriptor_header_t)) / sizeof(uint16_t)) * 3) + 1)

// returns length of string returned (including zero termination) or an error
zx_status_t usb_device_get_string_descriptor(usb_device_t* dev, uint8_t id,
                                             char* buf, size_t buflen);
