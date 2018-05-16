// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS;

typedef uint64_t bt_gatt_id_t;

typedef struct bt_gatt_uuid {
    uint8_t bytes[16];
} bt_gatt_uuid_t;

// ATT protocol error codes.
typedef enum {
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
} bt_gatt_err_t;

// Represents the status of a GATT operation.
typedef struct bt_gatt_status {
    // Represents errors reported by the host (i.e. not over ATT).
    zx_status_t status;

    // ATT protocol error.
    bt_gatt_err_t att_ecode;
} bt_gatt_status_t;

inline bool bt_gatt_status_is_success(bt_gatt_status_t* status) {
    return (status->status == ZX_OK) && (status->att_ecode == BT_GATT_ERR_NO_ERROR);
}

// Possible values for the characteristic properties bitfield.
typedef enum {
    BT_GATT_CHR_PROP_BROADCAST = 0x01,
    BT_GATT_CHR_PROP_READ = 0x02,
    BT_GATT_CHR_PROP_WRITE_WITHOUT_RESPONSE = 0x04,
    BT_GATT_CHR_PROP_WRITE = 0x08,
    BT_GATT_CHR_PROP_NOTIFY = 0x10,
    BT_GATT_CHR_PROP_INDICATE = 0x20,
    BT_GATT_CHR_PROP_AUTHENTICATED_SIGNED_WRITES = 0x40,
    BT_GATT_CHR_PROP_EXTENDED_PROPERTIES = 0x80,
} bt_gatt_chr_prop_t;

typedef enum {
    BT_GATT_CHR_EXT_PROP_RELIABLE_WRITE = 0x0100,
    BT_GATT_CHR_EXT_PROP_WRITABLE_AUXILIARIES = 0x0200,
} bt_gatt_chr_ext_prop_t;

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

// Callback interface to receive asynchronous events from a bt-gatt-svc device.
typedef struct bt_gatt_svc_callbacks {
    // Called to report the result of the |start| function. |status| will contain
    // the result of the characteristic discovery procedure if it was initiated by
    // |start|. The service will be ready to receive further requests once this
    // has been called successfully.
    void (*on_start)(void* cookie, bt_gatt_status_t status,
                     const bt_gatt_chr_t* characteristics, size_t len);

    // Called with the result of the |read_characteristic| function.
    void (*on_read_characteristic)(void* cookie, bt_gatt_status_t status, bt_gatt_id_t id,
                                   const uint8_t* value, size_t len);

    // Called with the result of the |enable_notifications| function.
    void (*on_enable_notifications)(void* cookie, bt_gatt_status_t status, bt_gatt_id_t id);

    // Called when a handle/value notification is received.
    void (*on_characteristic_notification)(void* cookie, bt_gatt_id_t id, const uint8_t* value,
                                           size_t len);
} bt_gatt_svc_callbacks_t;

typedef struct bt_gatt_svc_ops {
    // Starts this GATT service with |callbacks|. |on_start| will be called with
    // the result of the operation.
    //
    // Returns ZX_ERR_SHOULD_WAIT if this request is already in progress.
    // Returns ZX_ERR_BAD_STATE if this service has already been started.
    //
    // Once started, functions of |callback| can be invoked until stop() is
    // called.
    zx_status_t (*start)(void* ctx, bt_gatt_svc_callbacks_t* callbacks, void* cookie);

    // Stops this service and unregisters previously assigned |callbacks|.
    void (*stop)(void* ctx);

    // Reads the value of the characteristic with the given ID. Returns
    // ZX_ERR_BAD_STATE if the service has not been started yet.
    //
    // Returns ZX_ERR_SHOULD_WAIT if this request is already in progress.
    //
    // The |on_read_characteristic| callback will be called to asynchronously report the result
    // of this operation.
    zx_status_t (*read_characteristic)(void* ctx, bt_gatt_id_t id, void* cookie);

    // Enables notifications from the characteristic with the given ID. Returns
    // ZX_ERR_BAD_STATE if the service has not been started yet.
    //
    // Returns ZX_ERR_SHOULD_WAIT if this request is already in progress.
    //
    // The |on_enable_notifications| callback will be called to asynchronously report the result
    // of this operation.
    zx_status_t (*enable_notifications)(void* ctx, bt_gatt_id_t id, void* cookie);
} bt_gatt_svc_ops_t;

typedef struct bt_gatt_svc_proto {
    bt_gatt_svc_ops_t* ops;
    void* ctx;
} bt_gatt_svc_proto_t;

static inline zx_status_t bt_gatt_svc_start(bt_gatt_svc_proto_t* svc, bt_gatt_svc_callbacks_t* callbacks, void* cookie) {
    return svc->ops->start(svc->ctx, callbacks, cookie);
}

static inline void bt_gatt_svc_stop(bt_gatt_svc_proto_t* svc) {
    svc->ops->stop(svc->ctx);
}

static inline zx_status_t bt_gatt_svc_read_characteristic(bt_gatt_svc_proto_t* svc, bt_gatt_id_t id,
                                                          void* cookie) {
    return svc->ops->read_characteristic(svc->ctx, id, cookie);
}

static inline zx_status_t bt_gatt_svc_enable_notifications(bt_gatt_svc_proto_t* svc,
                                                           bt_gatt_id_t id, void* cookie) {
    return svc->ops->enable_notifications(svc->ctx, id, cookie);
}

__END_CDECLS;
