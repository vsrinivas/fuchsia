// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

__BEGIN_CDECLS;

/* HID Request Values */
#define USB_HID_GET_REPORT                  0x01
#define USB_HID_GET_IDLE                    0x02
#define USB_HID_GET_PROTOCOL                0x03
#define USB_HID_SET_REPORT                  0x09
#define USB_HID_SET_IDLE                    0x0A
#define USB_HID_SET_PROTOCOL                0x0B

typedef struct {
    uint8_t bDescriptorType;
    uint16_t wDescriptorLength;
} __attribute__((packed)) usb_hid_descriptor_entry_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    usb_hid_descriptor_entry_t descriptors[];
} __attribute__((packed)) usb_hid_descriptor_t;

__END_CDECLS;
