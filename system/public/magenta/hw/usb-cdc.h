// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// clang-format off

#include <magenta/compiler.h>

/* CDC Descriptor SubTypes */
#define USB_CDC_DST_HEADER                    0x00
#define USB_CDC_DST_CALL_MGMT                 0x01
#define USB_CDC_DST_ABSTRACT_CTRL_MGMT        0x02
#define USB_CDC_DST_DIRECT_LINE_MGMT          0x03
#define USB_CDC_DST_TELEPHONE_RINGER          0x04
#define USB_CDC_DST_TELEPHONE_CALL_REPORTING  0x05
#define USB_CDC_DST_UNION                     0x06
#define USB_CDC_DST_COUNTRY_SELECTION         0x07
#define USB_CDC_DST_TELEPHONE_OP_MODES        0x08
#define USB_CDC_DST_USB_TERMINAL              0x09
#define USB_CDC_DST_NETWORK_CHANNEL           0x0A
#define USB_CDC_DST_PROTOCOL_UNIT             0x0B
#define USB_CDC_DST_EXTENSION_UNIT            0x0C
#define USB_CDC_DST_MULTI_CHANNEL_MGMT        0x0D
#define USB_CDC_DST_CAPI_CTRL_MGMT            0x0E
#define USB_CDC_DST_ETHERNET                  0x0F
#define USB_CDC_DST_ATM_NETWORKING            0x10
#define USB_CDC_DST_WIRELESS_HANDSET_CTRL     0x11
#define USB_CDC_DST_MOBILE_DIRECT_LINE        0x12
#define USB_CDC_DST_MDLM_DETAIL               0x13
#define USB_CDC_DST_DEVICE_MGMT               0x14
#define USB_CDC_DST_OBEX                      0x15
#define USB_CDC_DST_COMMAND_SET               0x16
#define USB_CDC_DST_COMMAND_SET_DETAIL        0x17
#define USB_CDC_DST_TELEPHONE_CTRL            0x18
#define USB_CDC_DST_OBEX_SERVICE_ID           0x19
#define USB_CDC_DST_NCM                       0x1A

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;    // USB_DT_CS_INTERFACE
    uint8_t bDescriptorSubType; // USB_CDC_DST_HEADER
    uint16_t bcdCDC;
} __attribute__ ((packed)) usb_cs_header_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;    // USB_DT_CS_INTERFACE
    uint8_t bDescriptorSubType; // USB_CDC_DST_CALL_MGMT
    uint8_t bmCapabilities;
    uint8_t bDataInterface;
} __attribute__ ((packed)) usb_cs_call_mgmt_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;    // USB_DT_CS_INTERFACE
    uint8_t bDescriptorSubType; // USB_CDC_DST_ABSTRACT_CTRL_MGMT
    uint8_t bmCapabilities;
} __attribute__ ((packed)) usb_cs_abstract_ctrl_mgmt_interface_descriptor_t;

typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;    // USB_DT_CS_INTERFACE
    uint8_t bDescriptorSubType; // USB_CDC_DST_UNION
    uint8_t bControlInterface;
    uint8_t bSubordinateInterface[];
} __attribute__ ((packed)) usb_cs_union_interface_descriptor_t;

__BEGIN_CDECLS;
