// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_device_add, DeviceAdd,
        zx_status_t (C::*)(const pbus_dev_t* dev));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_device_add, ProtocolDeviceAdd,
        zx_status_t (C::*)(uint32_t proto_id, const pbus_dev_t* dev));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_register_protocol, RegisterProtocol,
        zx_status_t (C::*)(uint32_t proto_id, void* protocol, platform_proxy_cb_t proxy_cb,
                           void* proxy_cb_cookie));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_get_board_name, GetBoardName,
        const char* (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_set_board_info, SetBoardInfo,
        const char* (C::*)(const pbus_board_info_t*));

template <typename D>
constexpr void CheckPlatformBusProtocolSubclass() {
    static_assert(internal::has_pbus_device_add<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "DeviceAdd(const pbus_dev_t* dev)");
    static_assert(internal::has_pbus_protocol_device_add<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "ProtocolAdd(uint32_t proto_id, const pbus_dev_t* dev)");
    static_assert(internal::has_pbus_register_protocol<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "RegisterProtocol(uint32_t proto_id, void* protocol, platform_proxy_cb_t "
                  "proxy_cb, void* proxy_cb_cookie)");
    static_assert(internal::has_pbus_get_board_name<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "GetBoardName()");
    static_assert(internal::has_pbus_get_board_name<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "SetBoardInfo(const pbus_board_info_t* info)");
 }

}  // namespace internal
}  // namespace ddk
