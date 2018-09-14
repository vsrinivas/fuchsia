// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pci.fidl INSTEAD.

#pragma once

#include <ddk/protocol/pci.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_bar, PciGetBar,
                                     zx_status_t (C::*)(uint32_t bar_id, zx_pci_bar_t* out_res));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_map_bar, PciMapBar,
                                     zx_status_t (C::*)(uint32_t bar_id, uint32_t cache_policy,
                                                        void** out_vaddr_buffer, size_t* vaddr_size,
                                                        zx_handle_t* out_handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_enable_bus_master, PciEnableBusMaster,
                                     zx_status_t (C::*)(bool enable));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_reset_device, PciResetDevice,
                                     zx_status_t (C::*)());
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_map_interrupt, PciMapInterrupt,
                                     zx_status_t (C::*)(zx_status_t which_irq,
                                                        zx_handle_t* out_handle));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_query_irq_mode, PciQueryIrqMode,
                                     zx_status_t (C::*)(zx_pci_irq_mode_t mode,
                                                        uint32_t* out_max_irqs));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_set_irq_mode, PciSetIrqMode,
                                     zx_status_t (C::*)(zx_pci_irq_mode_t mode,
                                                        uint32_t requested_irq_count));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_device_info, PciGetDeviceInfo,
                                     zx_status_t (C::*)(zx_pcie_device_info_t* out_into));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_config_read, PciConfigRead,
                                     zx_status_t (C::*)(uint16_t offset, size_t width,
                                                        uint32_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_config_write, PciConfigWrite,
                                     zx_status_t (C::*)(uint16_t offset, size_t width,
                                                        uint32_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_next_capability, PciGetNextCapability,
                                     uint8_t (C::*)(uint8_t type, uint8_t offset));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_auxdata, PciGetAuxdata,
                                     zx_status_t (C::*)(const char* args, void* out_data_buffer,
                                                        size_t data_size, size_t* out_data_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_bti, PciGetBti,
                                     zx_status_t (C::*)(uint32_t index, zx_handle_t* out_bti));

template <typename D>
constexpr void CheckPciProtocolSubclass() {
    static_assert(internal::has_pci_protocol_get_bar<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_res");
    static_assert(internal::has_pci_protocol_map_bar<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciMapBar(uint32_t bar_id, uint32_t cache_policy, void** "
                  "out_vaddr_buffer, size_t* vaddr_size, zx_handle_t* out_handle");
    static_assert(internal::has_pci_protocol_enable_bus_master<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciEnableBusMaster(bool enable");
    static_assert(internal::has_pci_protocol_reset_device<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciResetDevice(");
    static_assert(internal::has_pci_protocol_map_interrupt<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciMapInterrupt(zx_status_t which_irq, zx_handle_t* out_handle");
    static_assert(internal::has_pci_protocol_query_irq_mode<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs");
    static_assert(internal::has_pci_protocol_set_irq_mode<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count");
    static_assert(internal::has_pci_protocol_get_device_info<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciGetDeviceInfo(zx_pcie_device_info_t* out_into");
    static_assert(internal::has_pci_protocol_config_read<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciConfigRead(uint16_t offset, size_t width, uint32_t* out_value");
    static_assert(internal::has_pci_protocol_config_write<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciConfigWrite(uint16_t offset, size_t width, uint32_t value");
    static_assert(internal::has_pci_protocol_get_next_capability<D>::value,
                  "PciProtocol subclasses must implement "
                  "uint8_t PciGetNextCapability(uint8_t type, uint8_t offset");
    static_assert(internal::has_pci_protocol_get_auxdata<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciGetAuxdata(const char* args, void* out_data_buffer, size_t "
                  "data_size, size_t* out_data_actual");
    static_assert(internal::has_pci_protocol_get_bti<D>::value,
                  "PciProtocol subclasses must implement "
                  "zx_status_t PciGetBti(uint32_t index, zx_handle_t* out_bti");
}

} // namespace internal
} // namespace ddk
