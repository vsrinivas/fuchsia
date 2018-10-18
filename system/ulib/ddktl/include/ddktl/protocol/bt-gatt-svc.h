// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bt_gatt_svc.banjo INSTEAD.

#pragma once

#include <ddk/protocol/bt-gatt-svc.h>
#include <ddktl/device-internal.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include "bt-gatt-svc-internal.h"

// DDK bt-gatt-svc-protocol support
//
// :: Proxies ::
//
// ddk::BtGattSvcProtocolProxy is a simple wrapper around
// bt_gatt_svc_protocol_t. It does not own the pointers passed to it
//
// :: Mixins ::
//
// ddk::BtGattSvcProtocol is a mixin class that simplifies writing DDK drivers
// that implement the bt-gatt-svc protocol. It doesn't set the base protocol.
//
// :: Examples ::
//
// // A driver that implements a ZX_PROTOCOL_BT_GATT_SVC device.
// class BtGattSvcDevice {
// using BtGattSvcDeviceType = ddk::Device<BtGattSvcDevice, /* ddk mixins */>;
//
// class BtGattSvcDevice : public BtGattSvcDeviceType,
//                         public ddk::BtGattSvcProtocol<BtGattSvcDevice> {
//   public:
//     BtGattSvcDevice(zx_device_t* parent)
//         : BtGattSvcDeviceType("my-bt-gatt-svc-protocol-device", parent) {}
//
//     void BtGattSvcConnect(bt_gatt_svc_connect_callback callback, void* cookie);
//
//     void BtGattSvcStop();
//
//     void BtGattSvcReadCharacteristic(bt_gatt_id_t id, bt_gatt_svc_read_characteristic_callback
//     callback, void* cookie);
//
//     void BtGattSvcReadLongCharacteristic(bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
//     bt_gatt_svc_read_long_characteristic_callback callback, void* cookie);
//
//     void BtGattSvcWriteCharacteristic(bt_gatt_id_t id, const void* buf_buffer, size_t buf_size,
//     bt_gatt_svc_write_characteristic_callback callback, void* cookie);
//
//     void BtGattSvcEnableNotifications(bt_gatt_id_t id, const bt_gatt_notification_value_t*
//     value_cb, bt_gatt_svc_enable_notifications_callback callback, void* cookie);
//
//     ...
// };

namespace ddk {

template <typename D>
class BtGattSvcProtocol : public internal::base_mixin {
public:
    BtGattSvcProtocol() {
        internal::CheckBtGattSvcProtocolSubclass<D>();
        bt_gatt_svc_protocol_ops_.connect = BtGattSvcConnect;
        bt_gatt_svc_protocol_ops_.stop = BtGattSvcStop;
        bt_gatt_svc_protocol_ops_.read_characteristic = BtGattSvcReadCharacteristic;
        bt_gatt_svc_protocol_ops_.read_long_characteristic = BtGattSvcReadLongCharacteristic;
        bt_gatt_svc_protocol_ops_.write_characteristic = BtGattSvcWriteCharacteristic;
        bt_gatt_svc_protocol_ops_.enable_notifications = BtGattSvcEnableNotifications;
    }

protected:
    bt_gatt_svc_protocol_ops_t bt_gatt_svc_protocol_ops_ = {};

private:
    // Connects to and starts characteristic discovery on the remote service.
    // |status| will contain the result of the characteristic discovery procedure if it was
    // initiated by |connect|. The service will be ready to receive further requests once this
    // has been called successfully and the |status| callback has been called with success.
    static void BtGattSvcConnect(void* ctx, bt_gatt_svc_connect_callback callback, void* cookie) {
        static_cast<D*>(ctx)->BtGattSvcConnect(callback, cookie);
    }
    // Stops this service and unregisters previously registered callbacks.
    static void BtGattSvcStop(void* ctx) { static_cast<D*>(ctx)->BtGattSvcStop(); }
    // Reads the value of the characteristic with the given ID.
    static void BtGattSvcReadCharacteristic(void* ctx, bt_gatt_id_t id,
                                            bt_gatt_svc_read_characteristic_callback callback,
                                            void* cookie) {
        static_cast<D*>(ctx)->BtGattSvcReadCharacteristic(id, callback, cookie);
    }
    // Reads the long value of the characteristic with the given ID.
    static void
    BtGattSvcReadLongCharacteristic(void* ctx, bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
                                    bt_gatt_svc_read_long_characteristic_callback callback,
                                    void* cookie) {
        static_cast<D*>(ctx)->BtGattSvcReadLongCharacteristic(id, offset, max_bytes, callback,
                                                              cookie);
    }
    static void BtGattSvcWriteCharacteristic(void* ctx, bt_gatt_id_t id, const void* buf_buffer,
                                             size_t buf_size,
                                             bt_gatt_svc_write_characteristic_callback callback,
                                             void* cookie) {
        static_cast<D*>(ctx)->BtGattSvcWriteCharacteristic(id, buf_buffer, buf_size, callback,
                                                           cookie);
    }
    // Enables notifications from the characteristic with the given ID. Returns
    // `ZX_ERR_BAD_STATE` if the service has not been started yet.
    // Returns `ZX_ERR_SHOULD_WAIT` if this request is already in progress.
    // The async callback will be called to asynchronously report the result
    // of this operation.
    static void BtGattSvcEnableNotifications(void* ctx, bt_gatt_id_t id,
                                             const bt_gatt_notification_value_t* value_cb,
                                             bt_gatt_svc_enable_notifications_callback callback,
                                             void* cookie) {
        static_cast<D*>(ctx)->BtGattSvcEnableNotifications(id, value_cb, callback, cookie);
    }
};

class BtGattSvcProtocolProxy {
public:
    BtGattSvcProtocolProxy() : ops_(nullptr), ctx_(nullptr) {}
    BtGattSvcProtocolProxy(const bt_gatt_svc_protocol_t* proto)
        : ops_(proto->ops), ctx_(proto->ctx) {}

    void GetProto(bt_gatt_svc_protocol_t* proto) {
        proto->ctx = ctx_;
        proto->ops = ops_;
    }
    bool is_valid() { return ops_ != nullptr; }
    void clear() {
        ctx_ = nullptr;
        ops_ = nullptr;
    }
    // Connects to and starts characteristic discovery on the remote service.
    // |status| will contain the result of the characteristic discovery procedure if it was
    // initiated by |connect|. The service will be ready to receive further requests once this
    // has been called successfully and the |status| callback has been called with success.
    void Connect(bt_gatt_svc_connect_callback callback, void* cookie) {
        ops_->connect(ctx_, callback, cookie);
    }
    // Stops this service and unregisters previously registered callbacks.
    void Stop() { ops_->stop(ctx_); }
    // Reads the value of the characteristic with the given ID.
    void ReadCharacteristic(bt_gatt_id_t id, bt_gatt_svc_read_characteristic_callback callback,
                            void* cookie) {
        ops_->read_characteristic(ctx_, id, callback, cookie);
    }
    // Reads the long value of the characteristic with the given ID.
    void ReadLongCharacteristic(bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
                                bt_gatt_svc_read_long_characteristic_callback callback,
                                void* cookie) {
        ops_->read_long_characteristic(ctx_, id, offset, max_bytes, callback, cookie);
    }
    void WriteCharacteristic(bt_gatt_id_t id, const void* buf_buffer, size_t buf_size,
                             bt_gatt_svc_write_characteristic_callback callback, void* cookie) {
        ops_->write_characteristic(ctx_, id, buf_buffer, buf_size, callback, cookie);
    }
    // Enables notifications from the characteristic with the given ID. Returns
    // `ZX_ERR_BAD_STATE` if the service has not been started yet.
    // Returns `ZX_ERR_SHOULD_WAIT` if this request is already in progress.
    // The async callback will be called to asynchronously report the result
    // of this operation.
    void EnableNotifications(bt_gatt_id_t id, const bt_gatt_notification_value_t* value_cb,
                             bt_gatt_svc_enable_notifications_callback callback, void* cookie) {
        ops_->enable_notifications(ctx_, id, value_cb, callback, cookie);
    }

private:
    bt_gatt_svc_protocol_ops_t* ops_;
    void* ctx_;
};

} // namespace ddk
