// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/i2c_impl.banjo INSTEAD.

#pragma once

#include <ddk/protocol/i2c-impl.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_protocol_get_bus_count, I2cImplGetBusCount,
                                     uint32_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_protocol_get_max_transfer_size,
                                     I2cImplGetMaxTransferSize,
                                     zx_status_t (C::*)(uint32_t bus_id, size_t* out_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_protocol_set_bitrate, I2cImplSetBitrate,
                                     zx_status_t (C::*)(uint32_t bus_id, uint32_t bitrate));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_protocol_transact, I2cImplTransact,
                                     zx_status_t (C::*)(uint32_t bus_id,
                                                        const i2c_impl_op_t* op_list,
                                                        size_t op_count));

template <typename D>
constexpr void CheckI2cImplProtocolSubclass() {
    static_assert(internal::has_i2c_impl_protocol_get_bus_count<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "uint32_t I2cImplGetBusCount(");
    static_assert(internal::has_i2c_impl_protocol_get_max_transfer_size<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "zx_status_t I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size");
    static_assert(internal::has_i2c_impl_protocol_set_bitrate<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "zx_status_t I2cImplSetBitrate(uint32_t bus_id, uint32_t bitrate");
    static_assert(internal::has_i2c_impl_protocol_transact<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "zx_status_t I2cImplTransact(uint32_t bus_id, const i2c_impl_op_t* op_list, "
                  "size_t op_count");
}

} // namespace internal
} // namespace ddk
