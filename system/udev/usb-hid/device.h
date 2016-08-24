// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/common/hid.h>
#include <ddk/protocol/input.h>
#include <ddk/protocol/usb-device.h>
#include <hw/usb.h>
#include <hw/usb-hid.h>

#include <system/listnode.h>

#include <stdint.h>
#include <stddef.h>
#include <threads.h>

#define HID_FLAGS_DEAD 1

typedef struct hid_report_size {
    int16_t id;
    input_report_size_t in_size;
    input_report_size_t out_size;
    input_report_size_t feat_size;
} hid_report_size_t;

typedef struct {
    mx_device_t dev;
    mx_device_t* usbdev;
    mx_driver_t* drv;

    usb_device_protocol_t* usb;
    usb_endpoint_t* endpt;
    iotxn_t* txn;

    uint32_t flags;
    uint8_t proto;
    uint8_t interface;

    usb_hid_descriptor_t* hid_desc;
    size_t hid_report_desc_len;
    const uint8_t* hid_report_desc;

#define HID_MAX_REPORT_IDS 16
    size_t num_reports;
    hid_report_size_t sizes[HID_MAX_REPORT_IDS];

    // list of opened devices
    struct list_node instance_list;
    mtx_t instance_lock;
} usb_hid_dev_t;

extern mx_protocol_device_t usb_hid_proto;

mx_status_t usb_hid_create_dev(usb_hid_dev_t** dev);
void usb_hid_cleanup_dev(usb_hid_dev_t* dev);

void usb_hid_load_hid_report_desc(usb_hid_dev_t* hid);
void usb_hid_process_closed(usb_hid_dev_t* hid);
void usb_hid_process_req(usb_hid_dev_t* hid, const uint8_t* buf, uint16_t len);

typedef struct {
    mx_device_t dev;
    usb_hid_dev_t* root;

    uint32_t flags;

    mx_hid_fifo_t fifo;

    struct list_node node;
} usb_hid_dev_instance_t;

extern mx_protocol_device_t usb_hid_instance_proto;

mx_status_t usb_hid_create_instance(usb_hid_dev_instance_t** dev);
void usb_hid_cleanup_instance(usb_hid_dev_instance_t* dev);

#define foreach_instance(root, instance) \
    list_for_every_entry(&root->instance_list, instance, usb_hid_dev_instance_t, node)
