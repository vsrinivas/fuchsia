// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_FIDL_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_FIDL_H_

#include <fuchsia/hardware/pci/c/banjo.h>
#include <lib/mmio/mmio-buffer.h>
#include <zircon/types.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/platform/kernel.h"

#if defined(__cplusplus)
extern "C" {
#endif  // defined(__cplusplus)

// This file declares a PCI interface that can be used by both C and C++ code.
// Under the hood, PCI operations are implemented using FIDL with C++ bindings,
// but since the iwl_pci_fidl struct is opaque and always accessed as a pointer,
// it can be used by C code.

void iwl_pci_ack_interrupt(const struct iwl_pci_fidl* fidl);
zx_status_t iwl_pci_read_config16(const struct iwl_pci_fidl* fidl, uint16_t offset,
                                  uint16_t* out_value);
zx_status_t iwl_pci_get_device_info(const struct iwl_pci_fidl* fidl, pci_device_info_t* out_info);
zx_status_t iwl_pci_get_bti(const struct iwl_pci_fidl* fidl, uint32_t index, zx_handle_t* out_bti);
void iwl_pci_get_interrupt_modes(const struct iwl_pci_fidl* fidl, pci_interrupt_modes_t* out_modes);
zx_status_t iwl_pci_set_interrupt_mode(const struct iwl_pci_fidl* fidl, pci_interrupt_mode_t mode,
                                       uint32_t requested_irq_count);
zx_status_t iwl_pci_set_bus_mastering(const struct iwl_pci_fidl* fidl, bool enabled);
zx_status_t iwl_pci_map_interrupt(const struct iwl_pci_fidl* fidl, uint32_t which_irq,
                                  zx_handle_t* out_interrupt);
zx_status_t iwl_pci_write_config8(const struct iwl_pci_fidl* fidl, uint16_t offset, uint8_t value);
zx_status_t iwl_pci_map_bar_buffer(const struct iwl_pci_fidl* fidl, uint32_t bar_id,
                                   uint32_t cache_policy, mmio_buffer_t* buffer);

struct zx_device;

zx_status_t iwl_pci_connect_fragment_protocol(struct zx_device* parent, const char* fragment_name,
                                              struct iwl_pci_fidl** fidl);

void iwl_pci_free(struct iwl_pci_fidl* fidl);

#if defined(__cplusplus)
}  // extern "C"
#endif  // defined(__cplusplus)

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_PLATFORM_PCI_FIDL_H_
