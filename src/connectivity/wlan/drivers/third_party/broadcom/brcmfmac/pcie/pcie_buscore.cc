// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"

#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipcommon.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Address mask for BAR0 window access.
constexpr uint32_t kBar0WindowAddressMask = (BRCMF_PCIE_BAR0_REG_SIZE - 1);

// Iff FromType is a pointer to volatile type, add volatile to ToType's pointer type.
template <typename FromType, typename ToType>
struct add_if_volatile {
  using type = typename std::conditional<
      std::is_volatile<typename std::remove_pointer<FromType>::type>::value,
      typename std::add_pointer<
          typename std::add_volatile<typename std::remove_pointer<ToType>::type>::type>::type,
      ToType>::type;
};

// Load/store a memory location, or use std::memory_order::memory_order_relaxed if it is volatile.

template <typename T>
T volatile_load(const T* src) {
  return *src;
}

template <typename T>
T volatile_load(volatile const T* src) {
  return reinterpret_cast<volatile const std::atomic<T>*>(src)->load(
      std::memory_order::memory_order_relaxed);
}

template <typename T>
void volatile_store(T* dst, T value) {
  *dst = value;
}

template <typename T>
void volatile_store(volatile T* dst, T value) {
  reinterpret_cast<volatile std::atomic<T>*>(dst)->store(value,
                                                         std::memory_order::memory_order_relaxed);
}

// Perform a (volatile-aware, and hence volatile-ordered) memcpy of as many bytes as possible using
// CopyType's size.
template <typename CopyType, typename DstType, typename SrcType>
size_t aligned_volatile_memcpy(DstType dst, SrcType src, size_t size) {
  ZX_DEBUG_ASSERT((reinterpret_cast<uintptr_t>(dst) % sizeof(CopyType)) == 0);
  ZX_DEBUG_ASSERT((reinterpret_cast<uintptr_t>(src) % sizeof(CopyType)) == 0);
  const size_t copy_count = size / sizeof(CopyType);
  auto dst_type = reinterpret_cast<typename add_if_volatile<DstType, CopyType*>::type>(dst);
  auto src_type = reinterpret_cast<typename add_if_volatile<SrcType, const CopyType*>::type>(src);
  for (size_t i = 0; i < copy_count; ++i) {
    volatile_store<CopyType>(dst_type + i, volatile_load<CopyType>(src_type + i));
  }
  // Note that this returns `size`, rounded down to the nearest `sizeof(CopyType)`.
  return copy_count * sizeof(CopyType);
}

// Perform a (volatile-aware, and hence volatile-ordered) memcpy.
template <typename DstType, typename SrcType>
void volatile_memcpy(DstType* dst, SrcType* src, size_t size) {
  size_t copied = 0;
  auto dst_char = reinterpret_cast<typename add_if_volatile<DstType, char*>::type>(dst);
  auto src_char = reinterpret_cast<typename add_if_volatile<SrcType, const char*>::type>(src);
  const uintptr_t common_align =
      (reinterpret_cast<uintptr_t>(dst) | reinterpret_cast<uintptr_t>(src)) % sizeof(uint32_t);
  switch (common_align) {
    case 0:
      copied +=
          aligned_volatile_memcpy<uint32_t>(dst_char + copied, src_char + copied, size - copied);
      [[fallthrough]];
    case 2:
      copied +=
          aligned_volatile_memcpy<uint16_t>(dst_char + copied, src_char + copied, size - copied);
      [[fallthrough]];
    default:
      copied +=
          aligned_volatile_memcpy<uint8_t>(dst_char + copied, src_char + copied, size - copied);
  }
}

}  // namespace

PcieBuscore::CoreRegs::CoreRegs() = default;

PcieBuscore::CoreRegs::CoreRegs(CoreRegs&& other) { swap(*this, other); }

PcieBuscore::CoreRegs& PcieBuscore::CoreRegs::operator=(PcieBuscore::CoreRegs other) {
  swap(*this, other);
  return *this;
}

void swap(PcieBuscore::CoreRegs& lhs, PcieBuscore::CoreRegs& rhs) {
  using std::swap;
  swap(lhs.parent_, rhs.parent_);
  swap(lhs.regs_offset_, rhs.regs_offset_);
}

PcieBuscore::CoreRegs::CoreRegs(PcieBuscore* parent, uint32_t base_address) {
  if (parent->AcquireBar0Window(base_address) != ZX_OK) {
    return;
  }

  parent_ = parent;
  regs_offset_ = reinterpret_cast<uintptr_t>(parent->regs_mmio_->get());
}

PcieBuscore::CoreRegs::~CoreRegs() {
  if (parent_ != nullptr) {
    parent_->ReleaseBar0Window();
  }
}

bool PcieBuscore::CoreRegs::is_valid() const { return parent_ != nullptr; }

uint32_t PcieBuscore::CoreRegs::RegRead(uint32_t offset) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  ZX_DEBUG_ASSERT(offset + sizeof(uint32_t) <= parent_->regs_mmio_->get_size());
  return reinterpret_cast<volatile const std::atomic<uint32_t>*>(regs_offset_ + offset)
      ->load(std::memory_order::memory_order_relaxed);
}

void PcieBuscore::CoreRegs::RegWrite(uint32_t offset, uint32_t value) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  ZX_DEBUG_ASSERT(offset + sizeof(uint32_t) <= parent_->regs_mmio_->get_size());
  reinterpret_cast<volatile std::atomic<uint32_t>*>(regs_offset_ + offset)
      ->store(value, std::memory_order::memory_order_relaxed);
}

volatile void* PcieBuscore::CoreRegs::GetRegPointer(uint32_t offset) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  ZX_DEBUG_ASSERT(offset < parent_->regs_mmio_->get_size());
  return reinterpret_cast<volatile void*>(regs_offset_ + offset);
}

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
    BRCMF_ERR("Failed to attach chip: %s\n", zx_status_get_string(status));
    return status;
  }

  // The BAR2 window may not be sized properly, so we re-write it to confirm.
  {
    const auto core = brcmf_chip_get_core(buscore->chip(), CHIPSET_PCIE2_CORE);
    CoreRegs core_regs(buscore.get(), core->base);
    if (!core_regs.is_valid()) {
      BRCMF_ERR("Failed to get PCIE2 core regs\n");
      return ZX_ERR_BAD_STATE;
    }
    core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_CONFIGADDR, BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG);
    const uint32_t bar2_config = core_regs.RegRead(BRCMF_PCIE_PCIE2REG_CONFIGDATA);
    core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_CONFIGDATA, bar2_config);
  }

  *out_buscore = std::move(buscore);
  return ZX_OK;
}

zx_status_t PcieBuscore::GetCoreRegs(uint16_t coreid, CoreRegs* out_core_regs) {
  const auto core = brcmf_chip_get_core(chip_, coreid);
  if (core == nullptr) {
    return ZX_ERR_NOT_FOUND;
  }

  CoreRegs core_regs(this, core->base);
  if (!core_regs.is_valid()) {
    return ZX_ERR_ALREADY_BOUND;
  }

  *out_core_regs = std::move(core_regs);
  return ZX_OK;
}

template <>
uint64_t PcieBuscore::TcmRead<uint64_t>(uint32_t offset) {
  ZX_DEBUG_ASSERT(offset + sizeof(uint64_t) <= tcm_mmio_->get_size());
  const auto address = reinterpret_cast<volatile const std::atomic<uint32_t>*>(
      reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  const uint32_t value_lo = address[0].load(std::memory_order::memory_order_relaxed);
  const uint64_t value_hi = address[1].load(std::memory_order::memory_order_relaxed);
  return (value_hi << 32) | value_lo;
}

template <>
void PcieBuscore::TcmWrite<uint64_t>(uint32_t offset, uint64_t value) {
  ZX_DEBUG_ASSERT(offset + sizeof(uint64_t) <= tcm_mmio_->get_size());
  const auto address = reinterpret_cast<volatile std::atomic<uint32_t>*>(
      reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  const uint32_t value_lo = static_cast<uint32_t>(value & 0xFFFFFFFF);
  const uint32_t value_hi = static_cast<uint32_t>(value >> 32);
  address[0].store(value_lo, std::memory_order::memory_order_relaxed);
  address[1].store(value_hi, std::memory_order::memory_order_relaxed);
}

void PcieBuscore::TcmRead(uint32_t offset, void* data, size_t size) {
  ZX_DEBUG_ASSERT(offset + size <= tcm_mmio_->get_size());
  auto src = reinterpret_cast<volatile const void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) +
                                                    offset);
  volatile_memcpy(data, src, size);
}

void PcieBuscore::TcmWrite(uint32_t offset, const void* data, size_t size) {
  ZX_DEBUG_ASSERT(offset + size <= tcm_mmio_->get_size());
  auto dst =
      reinterpret_cast<volatile void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  volatile_memcpy(dst, data, size);
}

void PcieBuscore::RamRead(uint32_t offset, void* data, size_t size) {
  TcmRead(chip_->rambase + offset, data, size);
}

void PcieBuscore::RamWrite(uint32_t offset, const void* data, size_t size) {
  TcmWrite(chip_->rambase + offset, data, size);
}

volatile void* PcieBuscore::GetTcmPointer(uint32_t offset) {
  ZX_DEBUG_ASSERT(offset < tcm_mmio_->get_size());
  return reinterpret_cast<volatile void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
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

uint32_t PcieBuscore::ConfigRead(uint32_t offset) {
  uint32_t value = 0;
  pci_proto_->ConfigRead32(offset, &value);
  return value;
}

void PcieBuscore::ConfigWrite(uint32_t offset, uint32_t value) {
  pci_proto_->ConfigWrite32(offset, value);
}

zx_status_t PcieBuscore::AcquireBar0Window(uint32_t base) {
  const uint32_t window_address = (base & ~kBar0WindowAddressMask);

  std::lock_guard lock(bar0_window_mutex_);
  if (window_address != bar0_window_address_) {
    if (bar0_window_refcount_ > 0) {
      BRCMF_ERR("Failed to acquire BAR0 window\n");
      return ZX_ERR_ALREADY_BOUND;
    }
    ConfigWrite(BRCMF_PCIE_BAR0_WINDOW, window_address);
    // Confirm the window change.
    if (ConfigRead(BRCMF_PCIE_BAR0_WINDOW) != window_address) {
      ConfigWrite(BRCMF_PCIE_BAR0_WINDOW, window_address);
    }
    bar0_window_address_ = window_address;
  }
  ++bar0_window_refcount_;
  return ZX_OK;
}

void PcieBuscore::ReleaseBar0Window() {
  std::lock_guard lock(bar0_window_mutex_);
  --bar0_window_refcount_;
}

uint32_t PcieBuscore::OpRead32(uint32_t address) {
  zx_status_t status = AcquireBar0Window(address);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  if (status != ZX_OK) {
    return 0;
  }
  const uint32_t bar0_address = address & kBar0WindowAddressMask;
  const uint32_t value = reinterpret_cast<volatile const std::atomic<int32_t>*>(
                             reinterpret_cast<uintptr_t>(regs_mmio_->get()) + bar0_address)
                             ->load(std::memory_order::memory_order_relaxed);
  ReleaseBar0Window();
  return value;
}

void PcieBuscore::OpWrite32(uint32_t address, uint32_t value) {
  zx_status_t status = AcquireBar0Window(address);
  ZX_DEBUG_ASSERT(status == ZX_OK);
  if (status != ZX_OK) {
    return;
  }
  const uint32_t bar0_address = address & kBar0WindowAddressMask;
  reinterpret_cast<volatile std::atomic<uint32_t>*>(reinterpret_cast<uintptr_t>(regs_mmio_->get()) +
                                                    bar0_address)
      ->store(value, std::memory_order::memory_order_relaxed);
  ReleaseBar0Window();
}

zx_status_t PcieBuscore::OpPrepare() {
  // Note: this logic now lives in PcieBuscore::Create().
  return ZX_OK;
}

zx_status_t PcieBuscore::OpReset(brcmf_chip* chip) {
  zx_status_t status = ZX_OK;
  if ((status = ResetDevice(chip)) != ZX_OK) {
    return status;
  }

  const auto core = brcmf_chip_get_core(chip, CHIPSET_PCIE2_CORE);
  CoreRegs core_regs(this, core->base);
  if (!core_regs.is_valid()) {
    return ZX_ERR_BAD_STATE;
  }

  uint32_t value = core_regs.RegRead(BRCMF_PCIE_PCIE2REG_MAILBOXINT);
  if (value != 0xffffffff) {
    core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_MAILBOXINT, value);
  }

  return ZX_OK;
}

void PcieBuscore::OpActivate(brcmf_chip* chip, uint32_t rstvec) { TcmWrite<uint32_t>(0, rstvec); }

zx_status_t PcieBuscore::ResetDevice(brcmf_chip* chip) {
  // Disable ASPM.
  const uint32_t lsc = ConfigRead(BRCMF_PCIE_REG_LINK_STATUS_CTRL);
  const uint32_t new_lsc = lsc & (~BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB);
  ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, new_lsc);

  // Watchdog reset.
  {
    const auto core = brcmf_chip_get_core(chip, CHIPSET_CHIPCOMMON_CORE);
    CoreRegs core_regs(this, core->base);
    if (!core_regs.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }
    core_regs.RegWrite(offsetof(chipcregs, watchdog), 4);
    zx::nanosleep(zx::deadline_after(zx::msec(100)));
  }

  // Restore ASPM.
  ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, lsc);

  const auto core = brcmf_chip_get_core(chip, CHIPSET_PCIE2_CORE);
  if (core->rev <= 13) {
    CoreRegs core_regs(this, core->base);
    if (!core_regs.is_valid()) {
      return ZX_ERR_BAD_STATE;
    }

    // Re-write these PCIE configuration registers, for unknown (but apparently important) reasons.
    static constexpr uint16_t kCfgOffsets[] = {
        BRCMF_PCIE_CFGREG_STATUS_CMD,        BRCMF_PCIE_CFGREG_PM_CSR,
        BRCMF_PCIE_CFGREG_MSI_CAP,           BRCMF_PCIE_CFGREG_MSI_ADDR_L,
        BRCMF_PCIE_CFGREG_MSI_ADDR_H,        BRCMF_PCIE_CFGREG_MSI_DATA,
        BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2, BRCMF_PCIE_CFGREG_RBAR_CTRL,
        BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1,    BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG,
        BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG};
    for (const uint16_t offset : kCfgOffsets) {
      core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_CONFIGADDR, offset);
      const uint32_t value = core_regs.RegRead(BRCMF_PCIE_PCIE2REG_CONFIGDATA);
      BRCMF_DBG(PCIE, "Buscore device reset: config offset=0x%04x, value=0x%04x\n", offset, value);
      core_regs.RegWrite(BRCMF_PCIE_PCIE2REG_CONFIGDATA, value);
    }
  }

  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
