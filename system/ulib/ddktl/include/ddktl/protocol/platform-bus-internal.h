// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_bus.banjo INSTEAD.

#pragma once

#include <ddk/protocol/platform-bus.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_device_add, PBusDeviceAdd,
                                     zx_status_t (C::*)(const pbus_dev_t* dev));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_protocol_device_add, PBusProtocolDeviceAdd,
                                     zx_status_t (C::*)(uint32_t proto_id, const pbus_dev_t* dev));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_register_protocol, PBusRegisterProtocol,
                                     zx_status_t (C::*)(uint32_t proto_id,
                                                        const void* protocol_buffer,
                                                        size_t protocol_size,
                                                        const platform_proxy_cb_t* proxy_cb));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_get_board_name, PBusGetBoardName,
                                     const char* (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_set_board_info, PBusSetBoardInfo,
                                     zx_status_t (C::*)(const pbus_board_info_t* info));

template <typename D>
constexpr void CheckPBusProtocolSubclass() {
    static_assert(internal::has_pbus_protocol_device_add<D>::value,
                  "PBusProtocol subclasses must implement "
                  "zx_status_t PBusDeviceAdd(const pbus_dev_t* dev");
    static_assert(internal::has_pbus_protocol_protocol_device_add<D>::value,
                  "PBusProtocol subclasses must implement "
                  "zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev");
    static_assert(internal::has_pbus_protocol_register_protocol<D>::value,
                  "PBusProtocol subclasses must implement "
                  "zx_status_t PBusRegisterProtocol(uint32_t proto_id, const void* "
                  "protocol_buffer, size_t protocol_size, const platform_proxy_cb_t* proxy_cb");
    static_assert(internal::has_pbus_protocol_get_board_name<D>::value,
                  "PBusProtocol subclasses must implement "
                  "const char* PBusGetBoardName(");
    static_assert(internal::has_pbus_protocol_set_board_info<D>::value,
                  "PBusProtocol subclasses must implement "
                  "zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info");
}

} // namespace internal
} // namespace ddk
