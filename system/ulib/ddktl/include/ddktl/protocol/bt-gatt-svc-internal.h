// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/bt_gatt_svc.banjo INSTEAD.

#pragma once

#include <ddk/protocol/bt-gatt-svc.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bt_gatt_svc_protocol_connect, BtGattSvcConnect,
                                     void (C::*)(bt_gatt_svc_connect_callback callback,
                                                 void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_bt_gatt_svc_protocol_stop, BtGattSvcStop, void (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_bt_gatt_svc_protocol_read_characteristic, BtGattSvcReadCharacteristic,
    void (C::*)(bt_gatt_id_t id, bt_gatt_svc_read_characteristic_callback callback, void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_bt_gatt_svc_protocol_read_long_characteristic, BtGattSvcReadLongCharacteristic,
    void (C::*)(bt_gatt_id_t id, uint16_t offset, size_t max_bytes,
                bt_gatt_svc_read_long_characteristic_callback callback, void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_bt_gatt_svc_protocol_write_characteristic, BtGattSvcWriteCharacteristic,
    void (C::*)(bt_gatt_id_t id, const void* buf_buffer, size_t buf_size,
                bt_gatt_svc_write_characteristic_callback callback, void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_bt_gatt_svc_protocol_enable_notifications, BtGattSvcEnableNotifications,
    void (C::*)(bt_gatt_id_t id, const bt_gatt_notification_value_t* value_cb,
                bt_gatt_svc_enable_notifications_callback callback, void* cookie));

template <typename D>
constexpr void CheckBtGattSvcProtocolSubclass() {
    static_assert(internal::has_bt_gatt_svc_protocol_connect<D>::value,
                  "BtGattSvcProtocol subclasses must implement "
                  "void BtGattSvcConnect(bt_gatt_svc_connect_callback callback, void* cookie");
    static_assert(internal::has_bt_gatt_svc_protocol_stop<D>::value,
                  "BtGattSvcProtocol subclasses must implement "
                  "void BtGattSvcStop(");
    static_assert(internal::has_bt_gatt_svc_protocol_read_characteristic<D>::value,
                  "BtGattSvcProtocol subclasses must implement "
                  "void BtGattSvcReadCharacteristic(bt_gatt_id_t id, "
                  "bt_gatt_svc_read_characteristic_callback callback, void* cookie");
    static_assert(
        internal::has_bt_gatt_svc_protocol_read_long_characteristic<D>::value,
        "BtGattSvcProtocol subclasses must implement "
        "void BtGattSvcReadLongCharacteristic(bt_gatt_id_t id, uint16_t offset, size_t max_bytes, "
        "bt_gatt_svc_read_long_characteristic_callback callback, void* cookie");
    static_assert(
        internal::has_bt_gatt_svc_protocol_write_characteristic<D>::value,
        "BtGattSvcProtocol subclasses must implement "
        "void BtGattSvcWriteCharacteristic(bt_gatt_id_t id, const void* buf_buffer, size_t "
        "buf_size, bt_gatt_svc_write_characteristic_callback callback, void* cookie");
    static_assert(
        internal::has_bt_gatt_svc_protocol_enable_notifications<D>::value,
        "BtGattSvcProtocol subclasses must implement "
        "void BtGattSvcEnableNotifications(bt_gatt_id_t id, const bt_gatt_notification_value_t* "
        "value_cb, bt_gatt_svc_enable_notifications_callback callback, void* cookie");
}

} // namespace internal
} // namespace ddk
