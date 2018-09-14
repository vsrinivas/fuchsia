// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/intel_gpu_core.fidl INSTEAD.

#pragma once

#include <ddk/protocol/intel-gpu-core.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_read_pci_config16,
                                     ZxIntelGpuCoreReadPciConfig16,
                                     zx_status_t (C::*)(uint16_t addr, uint16_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_map_pci_mmio,
                                     ZxIntelGpuCoreMapPciMmio,
                                     zx_status_t (C::*)(uint32_t pci_bar, void** out_buf_buffer,
                                                        size_t* buf_size));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_unmap_pci_mmio,
                                     ZxIntelGpuCoreUnmapPciMmio,
                                     zx_status_t (C::*)(uint32_t pci_bar));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_get_pci_bti,
                                     ZxIntelGpuCoreGetPciBti,
                                     zx_status_t (C::*)(uint32_t index, zx_handle_t* out_bti));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_zx_intel_gpu_core_protocol_register_interrupt_callback,
    ZxIntelGpuCoreRegisterInterruptCallback,
    zx_status_t (C::*)(const zx_intel_gpu_core_interrupt_t* callback, uint32_t interrupt_mask));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_unregister_interrupt_callback,
                                     ZxIntelGpuCoreUnregisterInterruptCallback,
                                     zx_status_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_gtt_get_size,
                                     ZxIntelGpuCoreGttGetSize, uint64_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_gtt_alloc,
                                     ZxIntelGpuCoreGttAlloc,
                                     zx_status_t (C::*)(uint64_t page_count, uint64_t* out_addr));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_gtt_free, ZxIntelGpuCoreGttFree,
                                     zx_status_t (C::*)(uint64_t addr));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_gtt_clear,
                                     ZxIntelGpuCoreGttClear, zx_status_t (C::*)(uint64_t addr));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_zx_intel_gpu_core_protocol_gtt_insert,
                                     ZxIntelGpuCoreGttInsert,
                                     zx_status_t (C::*)(uint64_t addr, zx_handle_t buffer,
                                                        uint64_t page_offset, uint64_t page_count));

template <typename D>
constexpr void CheckZxIntelGpuCoreProtocolSubclass() {
    static_assert(internal::has_zx_intel_gpu_core_protocol_read_pci_config16<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreReadPciConfig16(uint16_t addr, uint16_t* out_value");
    static_assert(internal::has_zx_intel_gpu_core_protocol_map_pci_mmio<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreMapPciMmio(uint32_t pci_bar, void** out_buf_buffer, "
                  "size_t* buf_size");
    static_assert(internal::has_zx_intel_gpu_core_protocol_unmap_pci_mmio<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreUnmapPciMmio(uint32_t pci_bar");
    static_assert(internal::has_zx_intel_gpu_core_protocol_get_pci_bti<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreGetPciBti(uint32_t index, zx_handle_t* out_bti");
    static_assert(internal::has_zx_intel_gpu_core_protocol_register_interrupt_callback<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreRegisterInterruptCallback(const "
                  "zx_intel_gpu_core_interrupt_t* callback, uint32_t interrupt_mask");
    static_assert(internal::has_zx_intel_gpu_core_protocol_unregister_interrupt_callback<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreUnregisterInterruptCallback(");
    static_assert(internal::has_zx_intel_gpu_core_protocol_gtt_get_size<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "uint64_t ZxIntelGpuCoreGttGetSize(");
    static_assert(internal::has_zx_intel_gpu_core_protocol_gtt_alloc<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreGttAlloc(uint64_t page_count, uint64_t* out_addr");
    static_assert(internal::has_zx_intel_gpu_core_protocol_gtt_free<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreGttFree(uint64_t addr");
    static_assert(internal::has_zx_intel_gpu_core_protocol_gtt_clear<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreGttClear(uint64_t addr");
    static_assert(internal::has_zx_intel_gpu_core_protocol_gtt_insert<D>::value,
                  "ZxIntelGpuCoreProtocol subclasses must implement "
                  "zx_status_t ZxIntelGpuCoreGttInsert(uint64_t addr, zx_handle_t buffer, uint64_t "
                  "page_offset, uint64_t page_count");
}

} // namespace internal
} // namespace ddk
