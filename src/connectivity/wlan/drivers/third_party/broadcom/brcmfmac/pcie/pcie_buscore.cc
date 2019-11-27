// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"

#include <lib/zx/time.h>
#include <threads.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <cstring>
#include <optional>

#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"

namespace wlan {
namespace brcmfmac {

PcieBuscore::PcieBuscore() = default;

PcieBuscore::~PcieBuscore() {
  if (chip_ != nullptr) {
    ResetDevice(chip_);
    brcmf_chip_detach(chip_);
    chip_ = nullptr;
  }
  tcm_mmio_.reset();
  regs_mmio_.reset();
  if (pci_proto_ != nullptr) {
    pci_proto_->EnableBusMaster(false);
    pci_proto_.reset();
  }
}

// static
zx_status_t PcieBuscore::Create(zx_device_t* device, std::unique_ptr<PcieBuscore>* out_buscore) {
  zx_status_t status = ZX_OK;

  // Get the PCI resources necessary to operate this device.
  auto pci_proto = std::make_unique<ddk::PciProtocolClient>();
  if ((status = ddk::PciProtocolClient::CreateFromDevice(device, pci_proto.get())) != ZX_OK) {
    BRCMF_ERR("ddk::PciProtocolClient::CreateFromDevice() failed: %s\n",
              zx_status_get_string(status));
    return status;
  }
  if ((status = pci_proto->EnableBusMaster(true)) != ZX_OK) {
    BRCMF_ERR("Failed to enable PCIE bus master: %s\n", zx_status_get_string(status));
    return status;
  }
  zx_pci_bar_t bar0_info = {};
  if ((status = pci_proto->GetBar(0, &bar0_info)) != ZX_OK) {
    BRCMF_ERR("Failed to get BAR0: %s\n", zx_status_get_string(status));
    return status;
  }
  zx_pci_bar_t bar2_info = {};
  if ((status = pci_proto->GetBar(2, &bar2_info)) != ZX_OK) {
    BRCMF_ERR("Failed to get BAR2: %s\n", zx_status_get_string(status));
    return status;
  }
  if (bar2_info.size == 0 || bar2_info.handle == ZX_HANDLE_INVALID) {
    BRCMF_ERR("BAR2 invalid: size=%zu, handle=%u\n", bar2_info.size, bar2_info.handle);
    return ZX_ERR_NO_RESOURCES;
  }

  // Map the MMIO regions.
  std::unique_ptr<ddk::MmioBuffer> regs_mmio;
  {
    size_t vmo_size = 0;
    if ((status = zx_vmo_get_size(bar0_info.handle, &vmo_size)) != ZX_OK) {
      BRCMF_ERR("Failed to get BAR0 VMO size: %s\n", zx_status_get_string(status));
      return status;
    }
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = ddk::MmioBuffer::Create(0, vmo_size, zx::vmo(bar0_info.handle),
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio)) != ZX_OK) {
      BRCMF_ERR("Failed to create BAR0 MmioBuffer: %s\n", zx_status_get_string(status));
      return status;
    }
    if (mmio->get_size() != BRCMF_PCIE_REG_MAP_SIZE) {
      BRCMF_ERR("BAR0 mapped size=%zu, expected %zu\n", mmio->get_size(), BRCMF_PCIE_REG_MAP_SIZE);
      return ZX_ERR_NO_RESOURCES;
    }
    regs_mmio = std::make_unique<ddk::MmioBuffer>(std::move(mmio.value()));
  }
  std::unique_ptr<ddk::MmioBuffer> tcm_mmio;
  {
    size_t vmo_size = 0;
    if ((status = zx_vmo_get_size(bar2_info.handle, &vmo_size)) != ZX_OK) {
      BRCMF_ERR("Failed to get BAR2 VMO size: %s\n", zx_status_get_string(status));
      return status;
    }
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = ddk::MmioBuffer::Create(0, vmo_size, zx::vmo(bar2_info.handle),
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio)) != ZX_OK) {
      BRCMF_ERR("Failed to create BAR2 MmioBuffer: %s\n", zx_status_get_string(status));
      return status;
    }
    tcm_mmio = std::make_unique<ddk::MmioBuffer>(std::move(mmio.value()));
  }

  auto buscore = std::make_unique<PcieBuscore>();
  buscore->pci_proto_ = std::move(pci_proto);
  buscore->regs_mmio_ = std::move(regs_mmio);
  buscore->tcm_mmio_ = std::move(tcm_mmio);

  if ((status = brcmf_chip_attach(buscore.get(), GetBuscoreOps(), &buscore->chip_)) != ZX_OK) {
    return status;
  }

  // The BAR2 window may not be sized properly, so we re-write it to confirm.
  buscore->SelectCore(buscore->chip_, CHIPSET_PCIE2_CORE);
  buscore->RegWrite<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGADDR, BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG);
  const uint32_t bar2_config = buscore->RegRead<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGDATA);
  buscore->RegWrite<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGDATA, bar2_config);

  *out_buscore = std::move(buscore);
  return ZX_OK;
}

void PcieBuscore::TcmRead(uint32_t offset, void* data, size_t size) {
  auto src_u8 =
      reinterpret_cast<const uint8_t*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  std::atomic_thread_fence(std::memory_order::memory_order_acquire);
  std::memcpy(data, src_u8, size);
}

void PcieBuscore::TcmWrite(uint32_t offset, const void* data, size_t size) {
  auto dst_u8 = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  std::memcpy(dst_u8, data, size);
  std::atomic_thread_fence(std::memory_order::memory_order_release);
}

void PcieBuscore::RamRead(uint32_t offset, void* data, size_t size) {
  TcmRead(chip_->rambase + offset, data, size);
}

void PcieBuscore::RamWrite(uint32_t offset, const void* data, size_t size) {
  TcmWrite(chip_->rambase + offset, data, size);
}

void* PcieBuscore::GetRegPointer(uint32_t offset) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(regs_mmio_->get()) + offset);
}

void* PcieBuscore::GetTcmPointer(uint32_t offset) {
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
}

zx_status_t PcieBuscore::CreateDmaBuffer(uint32_t cache_policy, size_t size,
                                         std::unique_ptr<DmaBuffer>* out_dma_buffer) {
  zx_status_t status = ZX_OK;
  zx::bti bti;
  if ((status = pci_proto_->GetBti(0, &bti)) != ZX_OK) {
    return status;
  }
  return DmaBuffer::Create(bti, cache_policy, size, out_dma_buffer);
}

void PcieBuscore::SelectCore(uint16_t coreid) { SelectCore(chip_, coreid); }

void PcieBuscore::SetRamsize(size_t ramsize) { chip_->ramsize = ramsize; }

brcmf_chip* PcieBuscore::chip() { return chip_; }

const brcmf_chip* PcieBuscore::chip() const { return chip_; }

// static
const brcmf_buscore_ops* PcieBuscore::GetBuscoreOps() {
  static constexpr brcmf_buscore_ops buscore_ops = {
      .read32 = [](void* ctx,
                   uint32_t address) { return static_cast<PcieBuscore*>(ctx)->OpRead32(address); },
      .write32 =
          [](void* ctx, uint32_t address, uint32_t value) {
            return static_cast<PcieBuscore*>(ctx)->OpWrite32(address, value);
          },
      .prepare = [](void* ctx) { return static_cast<PcieBuscore*>(ctx)->OpPrepare(); },
      .reset = [](void* ctx,
                  brcmf_chip* chip) { return static_cast<PcieBuscore*>(ctx)->OpReset(chip); },
      .activate =
          [](void* ctx, brcmf_chip* chip, uint32_t rstvec) {
            return static_cast<PcieBuscore*>(ctx)->OpActivate(chip, rstvec);
          },
  };
  return &buscore_ops;
}

uint32_t PcieBuscore::SetBar0Window(uint32_t address) {
  // We assume by design that all reads/writes through the BAR0 window come through on the same
  // thread, so we won't have to do any locking on it.
  ZX_DEBUG_ASSERT(thrd_equal(thrd_current(), []() {
    static thrd_t bar0_thread = thrd_current();
    return bar0_thread;
  }()));

  constexpr uint32_t kWindowMask = (BRCMF_PCIE_BAR0_REG_SIZE - 1);
  pci_proto_->ConfigWrite32(BRCMF_PCIE_BAR0_WINDOW, address & ~kWindowMask);
  return address & kWindowMask;
}

uint32_t PcieBuscore::ConfigRead(uint32_t offset) {
  uint32_t value = 0;
  pci_proto_->ConfigRead32(offset, &value);
  return value;
}

void PcieBuscore::ConfigWrite(uint32_t offset, uint32_t value) {
  pci_proto_->ConfigWrite32(offset, value);
}

uint32_t PcieBuscore::OpRead32(uint32_t address) {
  const auto window_address = SetBar0Window(address);
  return RegRead<uint32_t>(window_address);
}

void PcieBuscore::OpWrite32(uint32_t address, uint32_t value) {
  const auto window_address = SetBar0Window(address);
  RegWrite<uint32_t>(window_address, value);
}

zx_status_t PcieBuscore::OpPrepare() {
  // Note: this logic now lives in PcieBuscore::Create().
  return ZX_OK;
}

zx_status_t PcieBuscore::OpReset(brcmf_chip* chip) {
  ResetDevice(chip);
  const uint32_t val = RegRead<uint32_t>(BRCMF_PCIE_PCIE2REG_MAILBOXINT);
  if (val != 0xffffffff) {
    RegWrite<uint32_t>(BRCMF_PCIE_PCIE2REG_MAILBOXINT, val);
  }

  return ZX_OK;
}

void PcieBuscore::OpActivate(brcmf_chip* chip, uint32_t rstvec) { TcmWrite<uint32_t>(0, rstvec); }

void PcieBuscore::SelectCore(brcmf_chip* chip, uint16_t coreid) {
  const auto core = brcmf_chip_get_core(chip, coreid);
  if (core != nullptr) {
    ConfigWrite(BRCMF_PCIE_BAR0_WINDOW, core->base);
    if (ConfigRead(BRCMF_PCIE_BAR0_WINDOW) != core->base) {
      // Try once more.
      ConfigWrite(BRCMF_PCIE_BAR0_WINDOW, core->base);
    }
  } else {
    BRCMF_ERR("Cannot select core %d\n");
  }
}

void PcieBuscore::ResetDevice(brcmf_chip* chip) {
  // Disable ASPM.
  SelectCore(chip, CHIPSET_PCIE2_CORE);
  const uint32_t lsc = ConfigRead(BRCMF_PCIE_REG_LINK_STATUS_CTRL);
  const uint32_t new_lsc = lsc & (~BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB);
  ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, new_lsc);

  // Watchdog reset.
  SelectCore(chip, CHIPSET_CHIPCOMMON_CORE);
  RegWrite<uint32_t>(offsetof(chipcregs, watchdog), 4);
  zx::nanosleep(zx::deadline_after(zx::msec(100)));

  // Restore ASPM.
  SelectCore(chip, CHIPSET_PCIE2_CORE);
  ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, lsc);

  const auto core = brcmf_chip_get_core(chip, CHIPSET_PCIE2_CORE);
  if (core->rev <= 13) {
    // Re-write these PCIE configuration registers, for unknown (but apparently important) reasons.
    static constexpr uint16_t kCfgOffsets[] = {
        BRCMF_PCIE_CFGREG_STATUS_CMD,        BRCMF_PCIE_CFGREG_PM_CSR,
        BRCMF_PCIE_CFGREG_MSI_CAP,           BRCMF_PCIE_CFGREG_MSI_ADDR_L,
        BRCMF_PCIE_CFGREG_MSI_ADDR_H,        BRCMF_PCIE_CFGREG_MSI_DATA,
        BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2, BRCMF_PCIE_CFGREG_RBAR_CTRL,
        BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1,    BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG,
        BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG};
    for (const uint16_t offset : kCfgOffsets) {
      RegWrite<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGADDR, offset);
      const uint32_t value = RegRead<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGDATA);
      BRCMF_DBG(PCIE, "Buscore device reset: config offset=0x%04x, value=0x%04x\n", offset, value);
      RegWrite<uint32_t>(BRCMF_PCIE_PCIE2REG_CONFIGDATA, value);
    }
  }
}

}  // namespace brcmfmac
}  // namespace wlan
