// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/protocol/serial.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_get_info, GetInfo,
                                     zx_status_t (C::*)(serial_port_info_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_config, Config,
                                     zx_status_t (C::*)(uint32_t, uint32_t));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_enable, Enable,
                                     zx_status_t (C::*)(bool));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_read, Read,
                                     zx_status_t (C::*)(void*, size_t, size_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_write, Write,
                                     zx_status_t (C::*)(const void*, size_t, size_t*));

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(serial_has_set_notify_callback, SetNotifyCallback,
                                     zx_status_t (C::*)(serial_notify_cb, void*));

template <typename D>
constexpr void CheckSerialProtocolSubclass() {
    static_assert(internal::serial_has_get_info<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t GetInfo(serial_port_info_t* info)");
    static_assert(internal::serial_has_config<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t Config(uint32_t baud_rate, uint32_t flags)");
    static_assert(internal::serial_has_enable<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t Enable(bool enable)");
    static_assert(internal::serial_has_read<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t Read(void* buf, size_t length, size_t* out_actual)");
    static_assert(internal::serial_has_write<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t Write(const void* buf, size_t length, size_t* out_actual)");
    static_assert(internal::serial_has_set_notify_callback<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t SetNotifyCallback(serial_notify_cb cb, void* cookie)");
}

} // namespace internal
} // namespace ddk
