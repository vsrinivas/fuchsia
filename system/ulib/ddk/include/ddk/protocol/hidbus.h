// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/hidbus.fidl INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef uint8_t hid_description_type_t;
#define HID_DESCRIPTION_TYPE_REPORT UINT8_C(34)

typedef uint8_t hid_device_class_t;
#define HID_DEVICE_CLASS_OTHER UINT8_C(0)
#define HID_DEVICE_CLASS_KBD UINT8_C(1)
#define HID_DEVICE_CLASS_POINTER UINT8_C(2)
#define HID_DEVICE_CLASS_KBD_POINTER UINT8_C(3)
#define HID_DEVICE_CLASS_FIRST UINT8_C(0)
#define HID_DEVICE_CLASS_LAST UINT8_C(3)

typedef struct hid_info hid_info_t;
typedef uint8_t hid_report_type_t;
#define HID_REPORT_TYPE_INPUT UINT8_C(1)
#define HID_REPORT_TYPE_OUTPUT UINT8_C(2)
#define HID_REPORT_TYPE_FEATURE UINT8_C(3)

typedef uint8_t hid_protocol_t;
#define HID_PROTOCOL_BOOT UINT8_C(0)
#define HID_PROTOCOL_REPORT UINT8_C(0)

typedef struct hidbus_ifc hidbus_ifc_t;
typedef struct hidbus_protocol hidbus_protocol_t;

// Declarations

struct hid_info {
    uint8_t dev_num;
    hid_device_class_t device_class;
    bool boot_device;
};

typedef struct hidbus_ifc_ops {
    void (*io_queue)(void* ctx, const void* buf_buffer, size_t buf_size);
} hidbus_ifc_ops_t;

struct hidbus_ifc {
    hidbus_ifc_ops_t* ops;
    void* ctx;
};

// Queues a report received by the hidbus device.
static inline void hidbus_ifc_io_queue(const hidbus_ifc_t* proto, const void* buf_buffer,
                                       size_t buf_size) {
    proto->ops->io_queue(proto->ctx, buf_buffer, buf_size);
}

typedef struct hidbus_protocol_ops {
    zx_status_t (*query)(void* ctx, uint32_t options, hid_info_t* out_info);
    zx_status_t (*start)(void* ctx, const hidbus_ifc_t* ifc);
    void (*stop)(void* ctx);
    zx_status_t (*get_descriptor)(void* ctx, hid_description_type_t desc_type,
                                  void** out_data_buffer, size_t* data_size);
    zx_status_t (*get_report)(void* ctx, hid_report_type_t rpt_type, uint8_t rpt_id,
                              void* out_data_buffer, size_t data_size, size_t* out_data_actual);
    zx_status_t (*set_report)(void* ctx, hid_report_type_t rpt_type, uint8_t rpt_id,
                              const void* data_buffer, size_t data_size);
    zx_status_t (*get_idle)(void* ctx, uint8_t rpt_id, uint8_t* out_duration);
    zx_status_t (*set_idle)(void* ctx, uint8_t rpt_id, uint8_t duration);
    zx_status_t (*get_protocol)(void* ctx, hid_protocol_t* out_protocol);
    zx_status_t (*set_protocol)(void* ctx, hid_protocol_t protocol);
} hidbus_protocol_ops_t;

struct hidbus_protocol {
    hidbus_protocol_ops_t* ops;
    void* ctx;
};

// Obtain information about the hidbus device and supported features.
// Safe to call at any time.
static inline zx_status_t hidbus_query(const hidbus_protocol_t* proto, uint32_t options,
                                       hid_info_t* out_info) {
    return proto->ops->query(proto->ctx, options, out_info);
}
// Start the hidbus device. The device may begin queueing hid reports via
// ifc->io_queue before this function returns. It is an error to start an
// already-started hidbus device.
static inline zx_status_t hidbus_start(const hidbus_protocol_t* proto, const hidbus_ifc_t* ifc) {
    return proto->ops->start(proto->ctx, ifc);
}
// Stop the hidbus device. Safe to call if the hidbus is already stopped.
static inline void hidbus_stop(const hidbus_protocol_t* proto) {
    proto->ops->stop(proto->ctx);
}
// What are the ownership semantics with regards to the data buffer passed back?
// is len an input and output parameter?
static inline zx_status_t hidbus_get_descriptor(const hidbus_protocol_t* proto,
                                                hid_description_type_t desc_type,
                                                void** out_data_buffer, size_t* data_size) {
    return proto->ops->get_descriptor(proto->ctx, desc_type, out_data_buffer, data_size);
}
static inline zx_status_t hidbus_get_report(const hidbus_protocol_t* proto,
                                            hid_report_type_t rpt_type, uint8_t rpt_id,
                                            void* out_data_buffer, size_t data_size,
                                            size_t* out_data_actual) {
    return proto->ops->get_report(proto->ctx, rpt_type, rpt_id, out_data_buffer, data_size,
                                  out_data_actual);
}
static inline zx_status_t hidbus_set_report(const hidbus_protocol_t* proto,
                                            hid_report_type_t rpt_type, uint8_t rpt_id,
                                            const void* data_buffer, size_t data_size) {
    return proto->ops->set_report(proto->ctx, rpt_type, rpt_id, data_buffer, data_size);
}
static inline zx_status_t hidbus_get_idle(const hidbus_protocol_t* proto, uint8_t rpt_id,
                                          uint8_t* out_duration) {
    return proto->ops->get_idle(proto->ctx, rpt_id, out_duration);
}
static inline zx_status_t hidbus_set_idle(const hidbus_protocol_t* proto, uint8_t rpt_id,
                                          uint8_t duration) {
    return proto->ops->set_idle(proto->ctx, rpt_id, duration);
}
static inline zx_status_t hidbus_get_protocol(const hidbus_protocol_t* proto,
                                              hid_protocol_t* out_protocol) {
    return proto->ops->get_protocol(proto->ctx, out_protocol);
}
static inline zx_status_t hidbus_set_protocol(const hidbus_protocol_t* proto,
                                              hid_protocol_t protocol) {
    return proto->ops->set_protocol(proto->ctx, protocol);
}

__END_CDECLS;
