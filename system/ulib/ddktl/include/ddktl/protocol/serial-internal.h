// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/serial.fidl INSTEAD.

#pragma once

#include <ddk/protocol/serial.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_protocol_get_info, SerialGetInfo,
                                     zx_status_t (C::*)(serial_port_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_protocol_config, SerialConfig,
                                     zx_status_t (C::*)(uint32_t baud_rate, uint32_t flags));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_serial_protocol_open_socket, SerialOpenSocket,
                                     zx_status_t (C::*)(zx_handle_t* out_handle));

template <typename D>
constexpr void CheckSerialProtocolSubclass() {
    static_assert(internal::has_serial_protocol_get_info<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t SerialGetInfo(serial_port_info_t* out_info");
    static_assert(internal::has_serial_protocol_config<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t SerialConfig(uint32_t baud_rate, uint32_t flags");
    static_assert(internal::has_serial_protocol_open_socket<D>::value,
                  "SerialProtocol subclasses must implement "
                  "zx_status_t SerialOpenSocket(zx_handle_t* out_handle");
}

} // namespace internal
} // namespace ddk
