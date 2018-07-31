// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_get_bus_count, I2cImplGetBusCount,
        uint32_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_get_max_transfer_size, I2cImplGetMaxTransferSize,
        zx_status_t (C::*)(uint32_t, size_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_set_bitrate, I2cImplSetBitRate,
        zx_status_t (C::*)(uint32_t, uint32_t));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_impl_transact, I2cImplTransact,
        zx_status_t (C::*)(uint32_t, size_t*));

template <typename D>
constexpr void CheckI2cImplProtocolSubclass() {
    static_assert(internal::has_i2c_impl_transact<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "I2cImplGetBusCount()");
    static_assert(internal::has_i2c_impl_get_max_transfer_size<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "I2cImplGetMaxTransferSize(uint32_t bus_id, size_t* out_size)");
    static_assert(internal::has_i2c_impl_set_bitrate<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "I2cImplSetBitRate(uint32_t bus_id, uint32_t bitrate)");
    static_assert(internal::has_i2c_impl_transact<D>::value,
                  "I2cImplProtocol subclasses must implement "
                  "I2cImplTransact(uint32_t bus_id, uint16_t address, const void* write_buf, "
                  "size_t write_length, void* read_buf, size_t read_length)");
 }

}  // namespace internal
}  // namespace ddk
