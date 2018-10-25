// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/platform_device.banjo INSTEAD.

#pragma once

#include <ddk/protocol/platform-device.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_mmio, PDevGetMmio,
                                     zx_status_t (C::*)(uint32_t index, pdev_mmio_t* out_mmio));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_map_mmio, PDevMapMmio,
                                     zx_status_t (C::*)(uint32_t index, uint32_t cache_policy,
                                                        void** out_vaddr_buffer, size_t* vaddr_size,
                                                        uint64_t* out_paddr,
                                                        zx_handle_t* out_handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_interrupt, PDevGetInterrupt,
                                     zx_status_t (C::*)(uint32_t index, uint32_t flags,
                                                        zx_handle_t* out_irq));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_bti, PDevGetBti,
                                     zx_status_t (C::*)(uint32_t index, zx_handle_t* out_bti));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_smc, PDevGetSmc,
                                     zx_status_t (C::*)(uint32_t index, zx_handle_t* out_smc));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_device_info, PDevGetDeviceInfo,
                                     zx_status_t (C::*)(pdev_device_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_board_info, PDevGetBoardInfo,
                                     zx_status_t (C::*)(pdev_board_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_device_add, PDevDeviceAdd,
                                     zx_status_t (C::*)(uint32_t index,
                                                        const device_add_args_t* args,
                                                        zx_device_t** out_device));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pdev_protocol_get_protocol, PDevGetProtocol,
                                     zx_status_t (C::*)(uint32_t proto_id, uint32_t index,
                                                        void* out_out_protocol_buffer,
                                                        size_t out_protocol_size,
                                                        size_t* out_out_protocol_actual));

template <typename D>
constexpr void CheckPDevProtocolSubclass() {
    static_assert(internal::has_pdev_protocol_get_mmio<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevGetMmio(uint32_t index, pdev_mmio_t* out_mmio");
    static_assert(
        internal::has_pdev_protocol_map_mmio<D>::value,
        "PDevProtocol subclasses must implement "
        "zx_status_t PDevMapMmio(uint32_t index, uint32_t cache_policy, void** out_vaddr_buffer, "
        "size_t* vaddr_size, uint64_t* out_paddr, zx_handle_t* out_handle");
    static_assert(
        internal::has_pdev_protocol_get_interrupt<D>::value,
        "PDevProtocol subclasses must implement "
        "zx_status_t PDevGetInterrupt(uint32_t index, uint32_t flags, zx_handle_t* out_irq");
    static_assert(internal::has_pdev_protocol_get_bti<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevGetBti(uint32_t index, zx_handle_t* out_bti");
    static_assert(internal::has_pdev_protocol_get_smc<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevGetSmc(uint32_t index, zx_handle_t* out_smc");
    static_assert(internal::has_pdev_protocol_get_device_info<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevGetDeviceInfo(pdev_device_info_t* out_info");
    static_assert(internal::has_pdev_protocol_get_board_info<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevGetBoardInfo(pdev_board_info_t* out_info");
    static_assert(internal::has_pdev_protocol_device_add<D>::value,
                  "PDevProtocol subclasses must implement "
                  "zx_status_t PDevDeviceAdd(uint32_t index, const device_add_args_t* args, "
                  "zx_device_t** out_device");
    static_assert(
        internal::has_pdev_protocol_get_protocol<D>::value,
        "PDevProtocol subclasses must implement "
        "zx_status_t PDevGetProtocol(uint32_t proto_id, uint32_t index, void* "
        "out_out_protocol_buffer, size_t out_protocol_size, size_t* out_out_protocol_actual");
}

} // namespace internal
} // namespace ddk
