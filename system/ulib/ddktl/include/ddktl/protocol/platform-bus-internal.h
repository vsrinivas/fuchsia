// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_set_protocol, SetProtocol,
        zx_status_t (C::*)(uint32_t proto_id, void* protocol));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_wait_protocol, WaitProtocol,
        zx_status_t (C::*)(uint32_t proto_id));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_device_add, DeviceAdd,
        zx_status_t (C::*)(const pbus_dev_t* dev, uint32_t flags));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_device_enable, DeviceEnable,
        zx_status_t (C::*)(uint32_t vid, uint32_t pid, uint32_t did, bool enable));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_get_board_name, GetBoardName,
        const char* (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_set_board_info, SetBoardInfo,
        const char* (C::*)(const pbus_board_info_t*));

template <typename D>
constexpr void CheckPlatformBusProtocolSubclass() {
    static_assert(internal::has_pbus_set_protocol<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "SetProtocol(uint32_t proto_id, void* protocol)");
    static_assert(internal::has_pbus_wait_protocol<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "WaitProtocol(uint32_t proto_id)");
    static_assert(internal::has_pbus_device_add<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "DeviceAdd(const pbus_dev_t* dev, uint32_t flags)");
    static_assert(internal::has_pbus_device_enable<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "DeviceEnable(uint32_t vid, uint32_t pid, uint32_t did, bool enable)");
    static_assert(internal::has_pbus_get_board_name<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "GetBoardName()");
    static_assert(internal::has_pbus_get_board_name<D>::value,
                  "PlatformBusProtocol subclasses must implement "
                  "SetBoardInfo(const pbus_board_info_t* info)");
 }

}  // namespace internal
}  // namespace ddk
