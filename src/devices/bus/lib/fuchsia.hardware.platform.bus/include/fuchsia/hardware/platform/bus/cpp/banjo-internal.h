// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.platform.bus banjo file

#ifndef SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_INTERNAL_H_
#define SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_INTERNAL_H_

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_device_add, PBusDeviceAdd,
                                                    zx_status_t (C::*)(const pbus_dev_t* dev));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pbus_protocol_protocol_device_add,
                                                    PBusProtocolDeviceAdd,
                                                    zx_status_t (C::*)(uint32_t proto_id,
                                                                       const pbus_dev_t* dev));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_register_protocol, PBusRegisterProtocol,
    zx_status_t (C::*)(uint32_t proto_id, const uint8_t* protocol_buffer, size_t protocol_size));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_get_board_info, PBusGetBoardInfo,
    zx_status_t (C::*)(pdev_board_info_t* out_info));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_set_board_info, PBusSetBoardInfo,
    zx_status_t (C::*)(const pbus_board_info_t* info));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_set_bootloader_info, PBusSetBootloaderInfo,
    zx_status_t (C::*)(const pbus_bootloader_info_t* info));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_register_sys_suspend_callback, PBusRegisterSysSuspendCallback,
    zx_status_t (C::*)(const pbus_sys_suspend_t* suspend_cb));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_composite_device_add, PBusCompositeDeviceAdd,
    zx_status_t (C::*)(const pbus_dev_t* dev, uint64_t fragments, uint64_t fragments_count,
                       const char* primary_fragment));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pbus_protocol_add_composite, PBusAddComposite,
    zx_status_t (C::*)(const pbus_dev_t* dev, uint64_t fragments, uint64_t fragment_count,
                       const char* primary_fragment));

template <typename D>
constexpr void CheckPBusProtocolSubclass() {
  static_assert(internal::has_pbus_protocol_device_add<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusDeviceAdd(const pbus_dev_t* dev);");

  static_assert(internal::has_pbus_protocol_protocol_device_add<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusProtocolDeviceAdd(uint32_t proto_id, const pbus_dev_t* dev);");

  static_assert(internal::has_pbus_protocol_register_protocol<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusRegisterProtocol(uint32_t proto_id, const uint8_t* "
                "protocol_buffer, size_t protocol_size);");

  static_assert(internal::has_pbus_protocol_get_board_info<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusGetBoardInfo(pdev_board_info_t* out_info);");

  static_assert(internal::has_pbus_protocol_set_board_info<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusSetBoardInfo(const pbus_board_info_t* info);");

  static_assert(internal::has_pbus_protocol_set_bootloader_info<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusSetBootloaderInfo(const pbus_bootloader_info_t* info);");

  static_assert(
      internal::has_pbus_protocol_register_sys_suspend_callback<D>::value,
      "PBusProtocol subclasses must implement "
      "zx_status_t PBusRegisterSysSuspendCallback(const pbus_sys_suspend_t* suspend_cb);");

  static_assert(internal::has_pbus_protocol_composite_device_add<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusCompositeDeviceAdd(const pbus_dev_t* dev, uint64_t fragments, "
                "uint64_t fragments_count, const char* primary_fragment);");

  static_assert(internal::has_pbus_protocol_add_composite<D>::value,
                "PBusProtocol subclasses must implement "
                "zx_status_t PBusAddComposite(const pbus_dev_t* dev, uint64_t fragments, uint64_t "
                "fragment_count, const char* primary_fragment);");
}

}  // namespace internal
}  // namespace ddk

#endif  // SRC_DEVICES_BUS_LIB_FUCHSIA_HARDWARE_PLATFORM_BUS_INCLUDE_FUCHSIA_HARDWARE_PLATFORM_BUS_CPP_BANJO_INTERNAL_H_
