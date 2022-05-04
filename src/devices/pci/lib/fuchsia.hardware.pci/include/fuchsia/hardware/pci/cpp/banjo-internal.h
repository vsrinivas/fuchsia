// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// WARNING: THIS FILE IS MACHINE GENERATED. DO NOT EDIT.
// Generated from the fuchsia.hardware.pci banjo file

#ifndef SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_INTERNAL_H_
#define SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_INTERNAL_H_

#include <type_traits>

namespace ddk {
namespace internal {

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_get_device_info, PciGetDeviceInfo,
    zx_status_t (C::*)(pci_device_info_t* out_info));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_bar, PciGetBar,
                                                    zx_status_t (C::*)(uint32_t bar_id,
                                                                       pci_bar_t* out_result));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_set_bus_mastering,
                                                    PciSetBusMastering,
                                                    zx_status_t (C::*)(bool enabled));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_reset_device, PciResetDevice,
                                                    zx_status_t (C::*)());

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_ack_interrupt, PciAckInterrupt,
                                                    zx_status_t (C::*)());

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_map_interrupt, PciMapInterrupt,
    zx_status_t (C::*)(uint32_t which_irq, zx::interrupt* out_interrupt));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_interrupt_modes,
                                                    PciGetInterruptModes,
                                                    void (C::*)(pci_interrupt_modes_t* out_modes));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_set_interrupt_mode, PciSetInterruptMode,
    zx_status_t (C::*)(pci_interrupt_mode_t mode, uint32_t requested_irq_count));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_read_config8, PciReadConfig8,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint8_t* out_value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_read_config16, PciReadConfig16,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint16_t* out_value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_read_config32, PciReadConfig32,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint32_t* out_value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_write_config8, PciWriteConfig8,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint8_t value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_write_config16,
                                                    PciWriteConfig16,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint16_t value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_write_config32,
                                                    PciWriteConfig32,
                                                    zx_status_t (C::*)(uint16_t offset,
                                                                       uint32_t value));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_first_capability,
                                                    PciGetFirstCapability,
                                                    zx_status_t (C::*)(pci_capability_id_t id,
                                                                       uint8_t* out_offset));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_get_next_capability, PciGetNextCapability,
    zx_status_t (C::*)(pci_capability_id_t id, uint8_t start_offset, uint8_t* out_offset));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_get_first_extended_capability, PciGetFirstExtendedCapability,
    zx_status_t (C::*)(pci_extended_capability_id_t id, uint16_t* out_offset));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(
    has_pci_protocol_get_next_extended_capability, PciGetNextExtendedCapability,
    zx_status_t (C::*)(pci_extended_capability_id_t id, uint16_t start_offset,
                       uint16_t* out_offset));

DDKTL_INTERNAL_DECLARE_HAS_MEMBER_FN_WITH_SIGNATURE(has_pci_protocol_get_bti, PciGetBti,
                                                    zx_status_t (C::*)(uint32_t index,
                                                                       zx::bti* out_bti));

template <typename D>
constexpr void CheckPciProtocolSubclass() {
  static_assert(internal::has_pci_protocol_get_device_info<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetDeviceInfo(pci_device_info_t* out_info);");

  static_assert(internal::has_pci_protocol_get_bar<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetBar(uint32_t bar_id, pci_bar_t* out_result);");

  static_assert(internal::has_pci_protocol_set_bus_mastering<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciSetBusMastering(bool enabled);");

  static_assert(internal::has_pci_protocol_reset_device<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciResetDevice();");

  static_assert(internal::has_pci_protocol_ack_interrupt<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciAckInterrupt();");

  static_assert(internal::has_pci_protocol_map_interrupt<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciMapInterrupt(uint32_t which_irq, zx::interrupt* out_interrupt);");

  static_assert(internal::has_pci_protocol_get_interrupt_modes<D>::value,
                "PciProtocol subclasses must implement "
                "void PciGetInterruptModes(pci_interrupt_modes_t* out_modes);");

  static_assert(
      internal::has_pci_protocol_set_interrupt_mode<D>::value,
      "PciProtocol subclasses must implement "
      "zx_status_t PciSetInterruptMode(pci_interrupt_mode_t mode, uint32_t requested_irq_count);");

  static_assert(internal::has_pci_protocol_read_config8<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciReadConfig8(uint16_t offset, uint8_t* out_value);");

  static_assert(internal::has_pci_protocol_read_config16<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciReadConfig16(uint16_t offset, uint16_t* out_value);");

  static_assert(internal::has_pci_protocol_read_config32<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciReadConfig32(uint16_t offset, uint32_t* out_value);");

  static_assert(internal::has_pci_protocol_write_config8<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciWriteConfig8(uint16_t offset, uint8_t value);");

  static_assert(internal::has_pci_protocol_write_config16<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciWriteConfig16(uint16_t offset, uint16_t value);");

  static_assert(internal::has_pci_protocol_write_config32<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciWriteConfig32(uint16_t offset, uint32_t value);");

  static_assert(internal::has_pci_protocol_get_first_capability<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetFirstCapability(pci_capability_id_t id, uint8_t* out_offset);");

  static_assert(internal::has_pci_protocol_get_next_capability<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetNextCapability(pci_capability_id_t id, uint8_t start_offset, "
                "uint8_t* out_offset);");

  static_assert(internal::has_pci_protocol_get_first_extended_capability<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetFirstExtendedCapability(pci_extended_capability_id_t id, "
                "uint16_t* out_offset);");

  static_assert(internal::has_pci_protocol_get_next_extended_capability<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetNextExtendedCapability(pci_extended_capability_id_t id, "
                "uint16_t start_offset, uint16_t* out_offset);");

  static_assert(internal::has_pci_protocol_get_bti<D>::value,
                "PciProtocol subclasses must implement "
                "zx_status_t PciGetBti(uint32_t index, zx::bti* out_bti);");
}

}  // namespace internal
}  // namespace ddk

#endif  // SRC_DEVICES_PCI_LIB_FUCHSIA_HARDWARE_PCI_INCLUDE_FUCHSIA_HARDWARE_PCI_CPP_BANJO_INTERNAL_H_
