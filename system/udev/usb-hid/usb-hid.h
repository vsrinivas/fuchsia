// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

//#define USB_HID_DEBUG 1

#include <ddk/device.h>
#include <ddk/common/hid.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>
#include <hw/usb-hid.h>

#include <stdint.h>
#include <stddef.h>

typedef struct hid_report_size {
    int16_t id;
    input_report_size_t in_size;
    input_report_size_t out_size;
    input_report_size_t feat_size;
} hid_report_size_t;

typedef struct {
    mx_device_t dev;
    mx_device_t* usbdev;

    usb_device_protocol_t* usb;
    usb_endpoint_t* endpt;
    usb_request_t* req;

    uint32_t flags;
    uint8_t proto;
    uint8_t interface;

    usb_hid_descriptor_t* hid_desc;
    size_t hid_report_desc_len;
    uint8_t* hid_report_desc;

#define HID_MAX_REPORT_IDS 16
    size_t num_reports;
    hid_report_size_t sizes[HID_MAX_REPORT_IDS];

    mx_hid_fifo_t fifo;
} usb_hid_dev_t;

typedef struct hid_item {
    uint8_t bSize;
    uint8_t bType;
    uint8_t bTag;
    int64_t data;
} hid_item_t;

const uint8_t* hid_parse_short_item(const uint8_t* buf, const uint8_t* end, hid_item_t* item);
void hid_init_report_sizes(usb_hid_dev_t* hid);
int hid_find_report_id(input_report_id_t report_id, usb_hid_dev_t* hid);
void hid_read_report_sizes(const uint8_t* buf, size_t len, usb_hid_dev_t* hid);
input_report_size_t hid_max_report_size(usb_hid_dev_t* hid);
