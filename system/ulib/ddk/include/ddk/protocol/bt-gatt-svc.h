// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// BT SIG Base UUID for all 16/32 assigned UUID values.
//
//    "00000000-0000-1000-8000-00805F9B34FB"
//
// (see Core Spec v5.0, Vol 3, Part B, Section 2.5.1)
#define BT_GATT_BASE_UUID                                                       \
    {                                                                           \
        0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, \
            0x00, 0x00, 0x00, 0x00                                              \
    }

#define __BT_UUID_ASSIGNED_OFFSET 12

typedef uint64_t bt_gatt_id_t;

typedef struct bt_gatt_uuid {
    uint8_t bytes[16];
} bt_gatt_uuid_t;

// ATT protocol error codes.
enum {
    BT_GATT_ERR_NO_ERROR = 0x00,
    BT_GATT_ERR_INVALID_HANDLE = 0x01,
    BT_GATT_ERR_READ_NOT_PERMITTED = 0x02,
    BT_GATT_ERR_WRITE_NOT_PERMITTED = 0x03,
    BT_GATT_ERR_INVALID_PDU = 0x04,
    BT_GATT_ERR_INSUFFICIENT_AUTHENTICATION = 0x05,
    BT_GATT_ERR_REQUEST_NOT_SUPPORTED = 0x06,
    BT_GATT_ERR_INVALID_OFFSET = 0x07,
    BT_GATT_ERR_INSUFFICIENT_AUTHORIZATION = 0x08,
    BT_GATT_ERR_PREPARE_QUEUE_FULL = 0x09,
    BT_GATT_ERR_ATTRIBUTE_NOT_FOUND = 0x0A,
    BT_GATT_ERR_ATTRIBUTENOTLONG = 0x0B,
    BT_GATT_ERR_INSUFFICIENT_ENCRYPTION_KEY_SIZE = 0x0C,
    BT_GATT_ERR_INVALID_ATTRIBUTE_VALUE_LENGTH = 0x0D,
    BT_GATT_ERR_UNLIKELY_ERROR = 0x0E,
    BT_GATT_ERR_INSUFFICIENT_ENCRYPTION = 0x0F,
    BT_GATT_ERR_UNSUPPORTED_GROUP_TYPE = 0x10,
    BT_GATT_ERR_INSUFFICIENT_RESOURCES = 0x11,
};

typedef uint8_t bt_gatt_err_t;

// Represents the status of a GATT operation.
typedef struct bt_gatt_status {
    // Represents errors reported by the host (i.e. not over ATT).
    zx_status_t status;

    // ATT protocol error.
    bt_gatt_err_t att_ecode;
} bt_gatt_status_t;

inline bool bt_gatt_status_is_success(bt_gatt_status_t* status) {
    return (status->status == ZX_OK) &&
           (status->att_ecode == BT_GATT_ERR_NO_ERROR);
}

// Possible values for the characteristic properties bitfield.
enum bt_gatt_chr_prop {
    BT_GATT_CHR_PROP_BROADCAST = 0x01,
    BT_GATT_CHR_PROP_READ = 0x02,
    BT_GATT_CHR_PROP_WRITE_WITHOUT_RESPONSE = 0x04,
    BT_GATT_CHR_PROP_WRITE = 0x08,
    BT_GATT_CHR_PROP_NOTIFY = 0x10,
    BT_GATT_CHR_PROP_INDICATE = 0x20,
    BT_GATT_CHR_PROP_AUTHENTICATED_SIGNED_WRITES = 0x40,
    BT_GATT_CHR_PROP_EXTENDED_PROPERTIES = 0x80,
};

typedef uint8_t bt_gatt_chr_prop_t;

enum bt_gatt_chr_ext_prop {
    BT_GATT_CHR_EXT_PROP_RELIABLE_WRITE = 0x0100,
    BT_GATT_CHR_EXT_PROP_WRITABLE_AUXILIARIES = 0x0200,
};

typedef uint16_t bt_gatt_chr_ext_prop_t;

// Represents a GATT characteristic descriptor.
typedef struct bt_gatt_descriptor {
    bt_gatt_id_t id;
    bt_gatt_uuid_t type;
} bt_gatt_descriptor_t;

// Represents a GATT characteristic.
typedef struct bt_gatt_chr {
    bt_gatt_id_t id;
    bt_gatt_uuid_t type;

    // The bitmask of characteristic properties. The |extended_properties| field
    // is populated if the "Characteristic Extended Properties" descriptor is
    // present.
    //
    // See enums bt_gatt_chr_prop_t and bt_gatt_chr_ext_prop_t for possible
    // bit values.
    uint8_t properties;
    uint16_t extended_properties;

    size_t num_descriptors;
    bt_gatt_descriptor_t* descriptors;
} bt_gatt_chr_t;

// Generic status result callback for all functions return just a status.
typedef void (*bt_gatt_status_cb)(void* cookie, bt_gatt_status_t status,
                                  bt_gatt_id_t id);

// Result callback of the |connect| function. |status| will contain
// the result of the characteristic discovery procedure if it was initiated by
// |connect|. The service will be ready to receive further requests once this
// has been called successfully and the |status| callback has been called with success.
typedef void (*bt_gatt_connect_cb)(void* cookie, bt_gatt_status_t status,
                                   const bt_gatt_chr_t* characteristics,
                                   size_t characteristic_count);

// Result callback of the read related functions.
typedef void (*bt_gatt_read_characteristic_cb)(void* cookie,
                                               bt_gatt_status_t status,
                                               bt_gatt_id_t id,
                                               const uint8_t* value,
                                               size_t len);

// Value change notification callback of the |enable_notifications| function.
typedef void (*bt_gatt_notification_value_cb)(void* cookie, bt_gatt_id_t id,
                                              const uint8_t* value, size_t len);

typedef struct bt_gatt_svc_ops {
    // Connects to and starts characteristic discovery on the remote service.
    zx_status_t (*connect)(void* ctx, void* cookie,
                           bt_gatt_connect_cb connect_cb);

    // Stops this service and unregisters previously registered callbacks.
    void (*stop)(void* ctx);

    // Reads the value of the characteristic with the given ID.
    //
    // The |read_cb| callback will be called to asynchronously report the result
    // of this operation.
    zx_status_t (*read_characteristic)(void* ctx, bt_gatt_id_t id, void* cookie,
                                       bt_gatt_read_characteristic_cb read_cb);

    // Reads the long value of the characteristic with the given ID.
    //
    // The |read_cb| callback will be called to asynchronously report the result
    // of this operation.
    zx_status_t (*read_long_characteristic)(
        void* ctx, bt_gatt_id_t id, void* cookie, uint16_t offset,
        size_t max_bytes, bt_gatt_read_characteristic_cb read_cb);

    zx_status_t (*write_characteristic)(void* ctx, bt_gatt_id_t id, void* cookie,
                                        const uint8_t* buff, size_t len,
                                        bt_gatt_status_cb read_cb);

    // Enables notifications from the characteristic with the given ID. Returns
    // ZX_ERR_BAD_STATE if the service has not been started yet.
    //
    // Returns ZX_ERR_SHOULD_WAIT if this request is already in progress.
    //
    // The |status_cb| callback will be called to asynchronously report the result
    // of this operation.
    zx_status_t (*enable_notifications)(void* ctx, bt_gatt_id_t id, void* cookie,
                                        bt_gatt_status_cb status_cb,
                                        bt_gatt_notification_value_cb value_cb);
} bt_gatt_svc_ops_t;

typedef struct bt_gatt_svc_proto {
    bt_gatt_svc_ops_t* ops;
    void* ctx;
} bt_gatt_svc_proto_t;

static inline zx_status_t bt_gatt_svc_connect(bt_gatt_svc_proto_t* svc,
                                              void* cookie,
                                              bt_gatt_connect_cb connect_cb) {
    return svc->ops->connect(svc->ctx, cookie, connect_cb);
}

static inline void bt_gatt_svc_stop(bt_gatt_svc_proto_t* svc) {
    svc->ops->stop(svc->ctx);
}

static inline zx_status_t bt_gatt_svc_read_characteristic(
    bt_gatt_svc_proto_t* svc, bt_gatt_id_t id, void* cookie,
    bt_gatt_read_characteristic_cb read_cb) {
    return svc->ops->read_characteristic(svc->ctx, id, cookie, read_cb);
}

static inline zx_status_t bt_gatt_svc_read_long_characteristic(
    bt_gatt_svc_proto_t* svc, bt_gatt_id_t id, void* cookie, uint16_t offset,
    size_t max_bytes, bt_gatt_read_characteristic_cb read_cb) {
    return svc->ops->read_long_characteristic(svc->ctx, id, cookie, offset,
                                              max_bytes, read_cb);
}

static inline zx_status_t bt_gatt_svc_write_characteristic(
    bt_gatt_svc_proto_t* svc, bt_gatt_id_t id, void* cookie,
    const uint8_t* buff, size_t len, bt_gatt_status_cb status_cb) {
    return svc->ops->write_characteristic(svc->ctx, id, cookie, buff, len,
                                          status_cb);
}

static inline zx_status_t bt_gatt_svc_enable_notifications(
    bt_gatt_svc_proto_t* svc, bt_gatt_id_t id, void* cookie,
    bt_gatt_status_cb status_cb, bt_gatt_notification_value_cb value_cb) {
    return svc->ops->enable_notifications(svc->ctx, id, cookie, status_cb,
                                          value_cb);
}

// Convenience function to make a UUID from a 32-bit assigned value.
static inline bt_gatt_uuid_t bt_gatt_make_uuid32(uint32_t value) {
    bt_gatt_uuid_t retval = {.bytes = BT_GATT_BASE_UUID};

    retval.bytes[__BT_UUID_ASSIGNED_OFFSET] = (uint8_t)(value);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 1] = (uint8_t)(value >> 8);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 2] = (uint8_t)(value >> 16);
    retval.bytes[__BT_UUID_ASSIGNED_OFFSET + 3] = (uint8_t)(value >> 24);

    return retval;
}

// Convenience function to make a UUID from a 16-bit assigned value.
static inline bt_gatt_uuid_t bt_gatt_make_uuid16(uint16_t value) {
    return bt_gatt_make_uuid32((uint32_t)value);
}

// UUID comparsion.
// Note: this method only does a binary comparsion and doesn't break out low,
// mid, high, version, sequence, or node parts for indiviual comparison so
// doesn't conform to standard UUID sort.
static inline int bt_gatt_compare_uuid(const bt_gatt_uuid_t* u1,
                                       const bt_gatt_uuid_t* u2) {
    return memcmp(u1->bytes, u2->bytes, 16);
}

__END_CDECLS;
