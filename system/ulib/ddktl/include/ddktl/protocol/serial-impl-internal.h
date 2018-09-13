// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial_impl.fidl INSTEAD.

#pragma once

#include <ddk/protocol/serial-impl.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_get_info, SerialImplGetInfo,
                                     zx_status_t (C::*)(serial_port_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_config, SerialImplConfig,
                                     zx_status_t (C::*)(uint32_t baud_rate, uint32_t flags));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_enable, SerialImplEnable,
                                     zx_status_t (C::*)(bool enable));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_read, SerialImplRead,
                                     zx_status_t (C::*)(void* out_buf_buffer, size_t buf_size,
                                                        size_t* out_buf_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_write, SerialImplWrite,
                                     zx_status_t (C::*)(const void* buf_buffer, size_t buf_size,
                                                        size_t* out_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_impl_protocol_set_notify_callback,
                                     SerialImplSetNotifyCallback,
                                     zx_status_t (C::*)(const serial_notify_t* cb));

template <typename D>
constexpr void CheckSerialImplProtocolSubclass() {
    static_assert(internal::has_serial_impl_protocol_get_info<D>::value,
                  "SerialImplProtocol subclasses must implement "
                  "zx_status_t SerialImplGetInfo(serial_port_info_t* out_info");
    static_assert(internal::has_serial_impl_protocol_config<D>::value,
                  "SerialImplProtocol subclasses must implement "
                  "zx_status_t SerialImplConfig(uint32_t baud_rate, uint32_t flags");
    static_assert(internal::has_serial_impl_protocol_enable<D>::value,
                  "SerialImplProtocol subclasses must implement "
                  "zx_status_t SerialImplEnable(bool enable");
    static_assert(
        internal::has_serial_impl_protocol_read<D>::value,
        "SerialImplProtocol subclasses must implement "
        "zx_status_t SerialImplRead(void* out_buf_buffer, size_t buf_size, size_t* out_buf_actual");
    static_assert(
        internal::has_serial_impl_protocol_write<D>::value,
        "SerialImplProtocol subclasses must implement "
        "zx_status_t SerialImplWrite(const void* buf_buffer, size_t buf_size, size_t* out_actual");
    static_assert(internal::has_serial_impl_protocol_set_notify_callback<D>::value,
                  "SerialImplProtocol subclasses must implement "
                  "zx_status_t SerialImplSetNotifyCallback(const serial_notify_t* cb");
}

} // namespace internal
} // namespace ddk
