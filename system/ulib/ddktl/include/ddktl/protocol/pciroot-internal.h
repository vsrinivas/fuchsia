// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
//          MODIFY system/fidl/protocols/pciroot.banjo INSTEAD.

#pragma once

#include <ddk/protocol/pciroot.h>
#include <fbl/type_support.h>

namespace ddk {
namespace internal {

DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_auxdata, PcirootGetAuxdata,
                                     zx_status_t (C::*)(const char* args, void* out_data_buffer,
                                                        size_t data_size, size_t* out_data_actual));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_bti, PcirootGetBti,
                                     zx_status_t (C::*)(uint32_t bdf, uint32_t index,
                                                        zx_handle_t* out_bti));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_pci_platform_info,
                                     PcirootGetPciPlatformInfo,
                                     zx_status_t (C::*)(pci_platform_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_pci_irq_info, PcirootGetPciIrqInfo,
                                     zx_status_t (C::*)(pci_irq_info_t* out_info));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_driver_should_proxy_config,
                                     PcirootDriverShouldProxyConfig,
                                     zx_status_t (C::*)(bool* out_use_proxy));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_read8, PcirootConfigRead8,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint8_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_read16, PcirootConfigRead16,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint16_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_read32, PcirootConfigRead32,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint32_t* out_value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_write8, PcirootConfigWrite8,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint8_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_write16, PcirootConfigWrite16,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint16_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_config_write32, PcirootConfigWrite32,
                                     zx_status_t (C::*)(const pci_bdf_t* address, uint16_t offset,
                                                        uint32_t value));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_msi_alloc_block, PcirootMsiAllocBlock,
                                     zx_status_t (C::*)(uint64_t requested_irqs,
                                                        bool can_target_64bit,
                                                        msi_block_t* out_block));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_msi_free_block, PcirootMsiFreeBlock,
                                     zx_status_t (C::*)(const msi_block_t* block));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_msi_mask_unmask, PcirootMsiMaskUnmask,
                                     zx_status_t (C::*)(uint64_t msi_id, bool mask));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_get_address_space, PcirootGetAddressSpace,
                                     zx_status_t (C::*)(size_t len, pci_address_space_t type,
                                                        bool low, uint64_t* out_base));
DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pciroot_protocol_free_address_space,
                                     PcirootFreeAddressSpace,
                                     zx_status_t (C::*)(uint64_t base, size_t len,
                                                        pci_address_space_t type));

template <typename D>
constexpr void CheckPcirootProtocolSubclass() {
    static_assert(internal::has_pciroot_protocol_get_auxdata<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetAuxdata(const char* args, void* out_data_buffer, size_t "
                  "data_size, size_t* out_data_actual");
    static_assert(internal::has_pciroot_protocol_get_bti<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetBti(uint32_t bdf, uint32_t index, zx_handle_t* out_bti");
    static_assert(internal::has_pciroot_protocol_get_pci_platform_info<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetPciPlatformInfo(pci_platform_info_t* out_info");
    static_assert(internal::has_pciroot_protocol_get_pci_irq_info<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetPciIrqInfo(pci_irq_info_t* out_info");
    static_assert(internal::has_pciroot_protocol_driver_should_proxy_config<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootDriverShouldProxyConfig(bool* out_use_proxy");
    static_assert(internal::has_pciroot_protocol_config_read8<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootConfigRead8(const pci_bdf_t* address, uint16_t offset, "
                  "uint8_t* out_value");
    static_assert(internal::has_pciroot_protocol_config_read16<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootConfigRead16(const pci_bdf_t* address, uint16_t offset, "
                  "uint16_t* out_value");
    static_assert(internal::has_pciroot_protocol_config_read32<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootConfigRead32(const pci_bdf_t* address, uint16_t offset, "
                  "uint32_t* out_value");
    static_assert(
        internal::has_pciroot_protocol_config_write8<D>::value,
        "PcirootProtocol subclasses must implement "
        "zx_status_t PcirootConfigWrite8(const pci_bdf_t* address, uint16_t offset, uint8_t value");
    static_assert(internal::has_pciroot_protocol_config_write16<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootConfigWrite16(const pci_bdf_t* address, uint16_t offset, "
                  "uint16_t value");
    static_assert(internal::has_pciroot_protocol_config_write32<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootConfigWrite32(const pci_bdf_t* address, uint16_t offset, "
                  "uint32_t value");
    static_assert(internal::has_pciroot_protocol_msi_alloc_block<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootMsiAllocBlock(uint64_t requested_irqs, bool "
                  "can_target_64bit, msi_block_t* out_block");
    static_assert(internal::has_pciroot_protocol_msi_free_block<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootMsiFreeBlock(const msi_block_t* block");
    static_assert(internal::has_pciroot_protocol_msi_mask_unmask<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootMsiMaskUnmask(uint64_t msi_id, bool mask");
    static_assert(internal::has_pciroot_protocol_get_address_space<D>::value,
                  "PcirootProtocol subclasses must implement "
                  "zx_status_t PcirootGetAddressSpace(size_t len, pci_address_space_t type, bool "
                  "low, uint64_t* out_base");
    static_assert(
        internal::has_pciroot_protocol_free_address_space<D>::value,
        "PcirootProtocol subclasses must implement "
        "zx_status_t PcirootFreeAddressSpace(uint64_t base, size_t len, pci_address_space_t type");
}

} // namespace internal
} // namespace ddk
