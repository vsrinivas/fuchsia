// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>

__BEGIN_CDECLS;

enum {
    HID_DESC_TYPE_REPORT = 0x22,
};

enum {
    HID_REPORT_TYPE_INPUT = 1,
    HID_REPORT_TYPE_OUTPUT = 2,
    HID_REPORT_TYPE_FEATURE = 3,
};

enum {
    HID_PROTOCOL_BOOT = 0,
    HID_PROTOCOL_REPORT = 1,
};

enum {
    HID_DEV_CLASS_OTHER = 0,
    HID_DEV_CLASS_KBD = 1,
    HID_DEV_CLASS_POINTER = 2,
    HID_DEV_CLASS_KBD_POINTER = 3,

    HID_DEV_CLASS_FIRST = HID_DEV_CLASS_OTHER,
    HID_DEV_CLASS_LAST = HID_DEV_CLASS_KBD_POINTER,
};

typedef struct hid_info {
    uint8_t dev_num;
    uint8_t dev_class;
    bool boot_device;
} hid_info_t;

typedef struct hidbus_ifc {
    // Queues a report received by the hidbus device.
    void (*io_queue)(void* cookie, const uint8_t* buf, size_t len);
} hidbus_ifc_t;

typedef struct hidbus_protocol_ops {
    // Obtain information about the hidbus device and supported features.
    // Safe to call at any time.
    mx_status_t (*query)(void* ctx, uint32_t options, hid_info_t* info);

    // Start the hidbus device. The device may begin queueing hid reports via
    // ifc->io_queue before this function returns. It is an error to start an
    // already-started hidbus device.
    mx_status_t (*start)(void* ctx, hidbus_ifc_t* ifc, void* cookie);

    // Stop the hidbus device. Safe to call if the hidbus is already stopped.
    void (*stop)(void* ctx);

    // HID operations. See Device Class Definition for HID for details.
    mx_status_t (*get_descriptor)(void* ctx, uint8_t desc_type, void** data, size_t* len);
    mx_status_t (*get_report)(void* ctx, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    mx_status_t (*set_report)(void* ctx, uint8_t rpt_type, uint8_t rpt_id, void* data, size_t len);
    mx_status_t (*get_idle)(void* ctx, uint8_t rpt_id, uint8_t* duration);
    mx_status_t (*set_idle)(void* ctx, uint8_t rpt_id, uint8_t duration);
    mx_status_t (*get_protocol)(void* ctx, uint8_t* protocol);
    mx_status_t (*set_protocol)(void* ctx, uint8_t protocol);
} hidbus_protocol_ops_t;

typedef struct hidbus_protocol {
    hidbus_protocol_ops_t* ops;
    void* ctx;
} hidbus_protocol_t;

__END_CDECLS;
