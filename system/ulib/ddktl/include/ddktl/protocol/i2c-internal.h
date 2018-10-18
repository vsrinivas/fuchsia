// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/i2c.banjo INSTEAD.

#pragma once

#include <ddk/protocol/i2c.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_protocol_transact, I2cTransact,
                                     void (C::*)(const i2c_op_t* op_list, size_t op_count,
                                                 i2c_transact_callback callback, void* cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_protocol_get_max_transfer_size, I2cGetMaxTransferSize,
                                     zx_status_t (C::*)(size_t* out_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_protocol_get_interrupt, I2cGetInterrupt,
                                     zx_status_t (C::*)(uint32_t flags, zx_handle_t* out_irq));

template <typename D>
constexpr void CheckI2cProtocolSubclass() {
    static_assert(internal::has_i2c_protocol_transact<D>::value,
                  "I2cProtocol subclasses must implement "
                  "void I2cTransact(const i2c_op_t* op_list, size_t op_count, "
                  "i2c_transact_callback callback, void* cookie");
    static_assert(internal::has_i2c_protocol_get_max_transfer_size<D>::value,
                  "I2cProtocol subclasses must implement "
                  "zx_status_t I2cGetMaxTransferSize(size_t* out_size");
    static_assert(internal::has_i2c_protocol_get_interrupt<D>::value,
                  "I2cProtocol subclasses must implement "
                  "zx_status_t I2cGetInterrupt(uint32_t flags, zx_handle_t* out_irq");
}

} // namespace internal
} // namespace ddk
