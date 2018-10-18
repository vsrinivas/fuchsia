// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bt_gatt_svc.banjo INSTEAD.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

// Forward declarations

typedef uint16_t bt_gatt_chr_ext_prop_t;
#define BT_GATT_CHR_EXT_PROP_RELIABLE_WRITE UINT16_C(256)
#define BT_GATT_CHR_EXT_PROP_WRITABLE_AUXILIARIES UINT16_C(512)

// Possible values for the characteristic properties bitfield.
typedef uint8_t bt_gatt_chr_propr_t;
#define BT_GATT_CHR_PROPR_BROADCAST UINT8_C(1)
#define BT_GATT_CHR_PROPR_READ UINT8_C(2)
#define BT_GATT_CHR_PROPR_WRITE_WITHOUT_RESPONSE UINT8_C(4)
#define BT_GATT_CHR_PROPR_WRITE UINT8_C(8)
#define BT_GATT_CHR_PROPR_NOTIFY UINT8_C(16)
#define BT_GATT_CHR_PROPR_INDICATE UINT8_C(32)
#define BT_GATT_CHR_PROPR_AUTHENTICATED_SIGNED_WRITES UINT8_C(64)
#define BT_GATT_CHR_PROPR_EXTENDED_PROPERTIES UINT8_C(128)

typedef struct bt_gatt_uuid bt_gatt_uuid_t;
// ATT protocol error codes.
typedef uint8_t bt_gatt_err_t;
#define BT_GATT_ERR_NO_ERROR UINT8_C(0)
#define BT_GATT_ERR_INVALID_HANDLE UINT8_C(1)
#define BT_GATT_ERR_READ_NOT_PERMITTED UINT8_C(2)
#define BT_GATT_ERR_WRITE_NOT_PERMITTED UINT8_C(3)
#define BT_GATT_ERR_INVALID_PDU UINT8_C(4)
#define BT_GATT_ERR_INSUFFICIENT_AUTHENTICATION UINT8_C(5)
#define BT_GATT_ERR_REQUEST_NOT_SUPPORTED UINT8_C(6)
#define BT_GATT_ERR_INVALID_OFFSET UINT8_C(7)
#define BT_GATT_ERR_INSUFFICIENT_AUTHORIZATION UINT8_C(8)
#define BT_GATT_ERR_PREPARE_QUEUE_FULL UINT8_C(9)
#define BT_GATT_ERR_ATTRIBUTE_NOT_FOUND UINT8_C(10)
#define BT_GATT_ERR_ATTRIBUTENOTLONG UINT8_C(11)
#define BT_GATT_ERR_INSUFFICIENT_ENCRYPTION_KEY_SIZE UINT8_C(12)
#define BT_GATT_ERR_INVALID_ATTRIBUTE_VALUE_LENGTH UINT8_C(13)
#define BT_GATT_ERR_UNLIKELY_ERROR UINT8_C(14)
#define BT_GATT_ERR_INSUFFICIENT_ENCRYPTION UINT8_C(15)
#define BT_GATT_ERR_UNSUPPORTED_GROUP_TYPE UINT8_C(16)
#define BT_GATT_ERR_INSUFFICIENT_RESOURCES UINT8_C(17)

typedef struct bt_gatt_status bt_gatt_status_t;
typedef uint64_t bt_gatt_id_t;

typedef struct bt_gatt_notification_value bt_gatt_notification_value_t;
typedef struct bt_gatt_descriptor bt_gatt_descriptor_t;
typedef struct bt_gatt_chr bt_gatt_chr_t;
typedef struct bt_gatt_svc_protocol bt_gatt_svc_protocol_t;
typedef void (*bt_gatt_svc_connect_callback)(void* ctx, const bt_gatt_status_t* status,
                                             const bt_gatt_chr_t* characteristic_list,
                                             size_t characteristic_count);
typedef void (*bt_gatt_svc_read_characteristic_callback)(void* ctx, const bt_gatt_status_t* status,
                                                         bt_gatt_id_t id, const void* value_buffer,
                                                         size_t value_size);
typedef void (*bt_gatt_svc_read_long_characteristic_callback)(void* ctx,
                                                              const bt_gatt_status_t* status,
                                                              bt_gatt_id_t id,
                                                              const void* value_buffer,
                                                              size_t value_size);
typedef void (*bt_gatt_svc_write_characteristic_callback)(void* ctx, const bt_gatt_status_t* status,
                                                          bt_gatt_id_t id);
typedef void (*bt_gatt_svc_enable_notifications_callback)(void* ctx, const bt_gatt_status_t* status,
                                                          bt_gatt_id_t id);

// Declarations

struct bt_gatt_uuid {
    uint8_t bytes[16];
};

// Represents the status of a GATT operation.
struct bt_gatt_status {
    // Represents errors reported by the host (i.e. not over ATT).
    zx_status_t status;
    // ATT protocol error.
    bt_gatt_err_t att_ecode;
};

struct bt_gatt_notification_value {
    void (*callback)(void* ctx, bt_gatt_id_t id, const void* value_buffer, size_t value_size);
    void* ctx;
};

// Represents a GATT characteristic descriptor.
struct bt_gatt_descriptor {
    bt_gatt_id_t id;
    bt_gatt_uuid_t type;
};

// Represents a GATT characteristic.
struct bt_gatt_chr {
    bt_gatt_id_t id;
    bt_gatt_uuid_t type;
    // The bitmask of characteristic properties. The |extended_properties| field
    // is populated if the "Characteristic Extended Properties" descriptor is
    // present.
    // See enums |BtGattChrProp| and |BtGattChrExtProp| for possible bit values.
    uint8_t properties;
    uint16_t extended_properties;
    bt_gatt_descriptor_t* descriptor_list;
    size_t descriptor_count;
};

typedef struct bt_gatt_svc_protocol_ops {
    void (*connect)(void* ctx, bt_gatt_svc_connect_callback callback, void* cookie);
    void (*stop)(void* ctx);
    void (*read_characteristic)(void* ctx, bt_gatt_id_t id,
                                bt_gatt_svc_read_characteristic_callback callback, void* cookie);
    void (*read_long_characteristic)(void* ctx, bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
                                     bt_gatt_svc_read_long_characteristic_callback callback,
                                     void* cookie);
    void (*write_characteristic)(void* ctx, bt_gatt_id_t id, const void* buf_buffer,
                                 size_t buf_size,
                                 bt_gatt_svc_write_characteristic_callback callback, void* cookie);
    void (*enable_notifications)(void* ctx, bt_gatt_id_t id,
                                 const bt_gatt_notification_value_t* value_cb,
                                 bt_gatt_svc_enable_notifications_callback callback, void* cookie);
} bt_gatt_svc_protocol_ops_t;

struct bt_gatt_svc_protocol {
    bt_gatt_svc_protocol_ops_t* ops;
    void* ctx;
};

// Connects to and starts characteristic discovery on the remote service.
// |status| will contain the result of the characteristic discovery procedure if it was
// initiated by |connect|. The service will be ready to receive further requests once this
// has been called successfully and the |status| callback has been called with success.
static inline void bt_gatt_svc_connect(const bt_gatt_svc_protocol_t* proto,
                                       bt_gatt_svc_connect_callback callback, void* cookie) {
    proto->ops->connect(proto->ctx, callback, cookie);
}
// Stops this service and unregisters previously registered callbacks.
static inline void bt_gatt_svc_stop(const bt_gatt_svc_protocol_t* proto) {
    proto->ops->stop(proto->ctx);
}
// Reads the value of the characteristic with the given ID.
static inline void
bt_gatt_svc_read_characteristic(const bt_gatt_svc_protocol_t* proto, bt_gatt_id_t id,
                                bt_gatt_svc_read_characteristic_callback callback, void* cookie) {
    proto->ops->read_characteristic(proto->ctx, id, callback, cookie);
}
// Reads the long value of the characteristic with the given ID.
static inline void bt_gatt_svc_read_long_characteristic(
    const bt_gatt_svc_protocol_t* proto, bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
    bt_gatt_svc_read_long_characteristic_callback callback, void* cookie) {
    proto->ops->read_long_characteristic(proto->ctx, id, offset, max_bytes, callback, cookie);
}
static inline void
bt_gatt_svc_write_characteristic(const bt_gatt_svc_protocol_t* proto, bt_gatt_id_t id,
                                 const void* buf_buffer, size_t buf_size,
                                 bt_gatt_svc_write_characteristic_callback callback, void* cookie) {
    proto->ops->write_characteristic(proto->ctx, id, buf_buffer, buf_size, callback, cookie);
}
// Enables notifications from the characteristic with the given ID. Returns
// `ZX_ERR_BAD_STATE` if the service has not been started yet.
// Returns `ZX_ERR_SHOULD_WAIT` if this request is already in progress.
// The async callback will be called to asynchronously report the result
// of this operation.
static inline void
bt_gatt_svc_enable_notifications(const bt_gatt_svc_protocol_t* proto, bt_gatt_id_t id,
                                 const bt_gatt_notification_value_t* value_cb,
                                 bt_gatt_svc_enable_notifications_callback callback, void* cookie) {
    proto->ops->enable_notifications(proto->ctx, id, value_cb, callback, cookie);
}

__END_CDECLS;
