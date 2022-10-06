/******************************************************************************
  SPDX-License-Identifier: BSD-3-Clause

  Copyright (c) 2001-2015, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

   3. Neither the name of the Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD$*/

#include "e1000_api.h"
#include "lib/device-protocol/pci.h"

/*
 * NOTE: the following routines using the e1000
 *  naming style are provided to the shared
 *  code but are OS specific
 */

struct e1000_pci {
  std::unique_ptr<ddk::Pci> pci;
};

void e1000_write_pci_cfg(struct e1000_hw* hw, u32 reg, u16* value) {
  hw2pci(hw)->pci->WriteConfig16(reg, *value);
}

void e1000_read_pci_cfg(struct e1000_hw* hw, u32 reg, u16* value) {
  hw2pci(hw)->pci->ReadConfig16(reg, value);
}

void e1000_pci_set_mwi(struct e1000_hw* hw) {
  hw2pci(hw)->pci->WriteConfig16(fidl::ToUnderlying(fuchsia_hardware_pci::Config::kCommand),
                                 hw->bus.pci_cmd_word | CMD_MEM_WRT_INVALIDATE);
}

void e1000_pci_clear_mwi(struct e1000_hw* hw) {
  hw2pci(hw)->pci->WriteConfig16(fidl::ToUnderlying(fuchsia_hardware_pci::Config::kCommand),
                                 (hw->bus.pci_cmd_word & ~CMD_MEM_WRT_INVALIDATE));
}

/*
 * Read the PCI Express capabilities
 */
int32_t e1000_read_pcie_cap_reg(struct e1000_hw* hw, u32 reg, u16* value) {
  uint8_t offset;
  zx_status_t st =
      hw2pci(hw)->pci->GetFirstCapability(fuchsia_hardware_pci::CapabilityId::kPciExpress, &offset);
  if (st != ZX_OK) {
    return E1000_ERR_CONFIG;
  }

  hw2pci(hw)->pci->ReadConfig16(offset + reg, value);
  return E1000_SUCCESS;
}

/*
 * Write the PCI Express capabilities
 */
int32_t e1000_write_pcie_cap_reg(struct e1000_hw* hw, u32 reg, u16* value) {
  uint8_t offset;
  zx_status_t st =
      hw2pci(hw)->pci->GetFirstCapability(fuchsia_hardware_pci::CapabilityId::kPciExpress, &offset);
  if (st != ZX_OK) {
    return E1000_ERR_CONFIG;
  }

  hw2pci(hw)->pci->WriteConfig16(offset + reg, *value);
  return E1000_SUCCESS;
}

zx_status_t e1000_pci_set_bus_mastering(const struct e1000_pci* pci, bool enabled) {
  return pci->pci->SetBusMastering(enabled);
}

zx_status_t e1000_pci_ack_interrupt(const struct e1000_pci* pci) {
  return pci->pci->AckInterrupt();
}

zx_status_t e1000_pci_read_config16(const struct e1000_pci* pci, uint16_t offset,
                                    uint16_t* out_value) {
  return pci->pci->ReadConfig16(offset, out_value);
}

zx_status_t e1000_pci_get_device_info(const struct e1000_pci* pci, pci_device_info_t* out_info) {
  fuchsia_hardware_pci::wire::DeviceInfo info;
  zx_status_t status = pci->pci->GetDeviceInfo(&info);
  if (status == ZX_OK) {
    *out_info = ddk::convert_device_info_to_banjo(info);
  }
  return status;
}

zx_status_t e1000_pci_map_bar_buffer(const struct e1000_pci* pci, uint32_t bar_id,
                                     uint32_t cache_policy, mmio_buffer_t* mmio) {
  return pci->pci->MapMmio(bar_id, cache_policy, mmio);
}

zx_status_t e1000_pci_get_bar(const struct e1000_pci* pci, uint32_t bar_id, pci_bar_t* out_result) {
  fuchsia_hardware_pci::wire::Bar bar;
  fidl::Arena arena;
  zx_status_t status = pci->pci->GetBar(arena, bar_id, &bar);
  if (status == ZX_OK) {
    *out_result = ddk::convert_bar_to_banjo(std::move(bar));
  }
  return status;
}

zx_status_t e1000_pci_get_bti(const struct e1000_pci* pci, uint32_t index, zx_handle_t* out_bti) {
  zx::bti bti;
  zx_status_t status = pci->pci->GetBti(index, &bti);
  if (status == ZX_OK) {
    *out_bti = bti.release();
  }
  return status;
}

zx_status_t e1000_pci_configure_interrupt_mode(const struct e1000_pci* pci,
                                               uint32_t requested_irq_count,
                                               pci_interrupt_mode_t* out_mode) {
  fuchsia_hardware_pci::InterruptMode mode;
  zx_status_t status = pci->pci->ConfigureInterruptMode(requested_irq_count, &mode);
  if (status == ZX_OK) {
    *out_mode = static_cast<pci_interrupt_mode_t>(mode);
  }
  return status;
}

zx_status_t e1000_pci_map_interrupt(const struct e1000_pci* pci, uint32_t which_irq,
                                    zx_handle_t* out_interrupt) {
  zx::interrupt interrupt;
  zx_status_t status = pci->pci->MapInterrupt(which_irq, &interrupt);
  if (status == ZX_OK) {
    *out_interrupt = interrupt.release();
  }
  return status;
}

zx_status_t e1000_pci_connect_fragment_protocol(struct zx_device* parent, const char* fragment_name,
                                                struct e1000_pci** pci) {
  (*pci) = new struct e1000_pci;
  (*pci)->pci = std::make_unique<ddk::Pci>(parent, fragment_name);

  if (!(*pci)->pci->is_valid()) {
    return ZX_ERR_INTERNAL;
  }

  return ZX_OK;
}

void e1000_pci_free(struct e1000_pci* pci) {
  pci->pci.reset();
  delete pci;
}

bool e1000_pci_is_valid(const struct e1000_pci* pci) { return pci->pci->is_valid(); }
