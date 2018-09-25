// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_transact, I2cTransact,
        zx_status_t (C::*)(i2c_op_t*, size_t, i2c_transact_cb, void*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_get_max_transfer_size, I2cGetMaxTransferSize,
        zx_status_t (C::*)(size_t*));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_i2c_get_interrupt, I2cGetInterrupt,
        zx_status_t (C::*)(uint32_t, zx_handle_t*));

template <typename D>
constexpr void CheckI2cProtocolSubclass() {
    static_assert(internal::has_i2c_transact<D>::value,
                  "I2cProtocol subclasses must implement "
                  "I2cTransact(i2c_op_t* ops, size_t cnt, i2c_transact_cb transact_cb,"
                  "void* cookie)");
    static_assert(internal::has_i2c_get_max_transfer_size<D>::value,
                  "I2cProtocol subclasses must implement "
                  "I2cGetMaxTransferSize(size_t* out_size)");
    static_assert(internal::has_i2c_get_interrupt<D>::value,
                  "I2cProtocol subclasses must implement "
                  "I2cGetInterrupt(uint32_t flags, zx_handle_t* out_handle)");
 }

}  // namespace internal
}  // namespace ddk
