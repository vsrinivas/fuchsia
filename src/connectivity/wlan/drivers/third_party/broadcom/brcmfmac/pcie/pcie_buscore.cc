// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_buscore.h"

#include <lib/zx/time.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

#include <ddk/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_regs.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/debug.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/dma_buffer.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/pcie/pcie_regs.h"

namespace wlan {
namespace brcmfmac {
namespace {

// Address mask for BAR0 window access.
constexpr uint32_t kBar0WindowAddressMask = (BRCMF_PCIE_BAR0_REG_SIZE - 1);

// Wait interval after resetting the chipcommon watchdog.
constexpr zx::duration kChipCommonWatchdogWait = zx::msec(10);

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

PcieBuscore::PcieRegisterWindow::PcieRegisterWindow() = default;

PcieBuscore::PcieRegisterWindow::PcieRegisterWindow(PcieRegisterWindow&& other) {
  swap(*this, other);
}

PcieBuscore::PcieRegisterWindow& PcieBuscore::PcieRegisterWindow::PcieRegisterWindow::operator=(
    PcieBuscore::PcieRegisterWindow other) {
  swap(*this, other);
  return *this;
}

void swap(PcieBuscore::PcieRegisterWindow& lhs, PcieBuscore::PcieRegisterWindow& rhs) {
  using std::swap;
  swap(lhs.parent_, rhs.parent_);
  swap(lhs.base_address_, rhs.base_address_);
  swap(lhs.size_, rhs.size_);
}

PcieBuscore::PcieRegisterWindow::PcieRegisterWindow(PcieBuscore* parent, uintptr_t base_address,
                                                    size_t size)
    : parent_(parent), base_address_(base_address), size_(size) {}

PcieBuscore::PcieRegisterWindow::~PcieRegisterWindow() {
  if (parent_ != nullptr) {
    parent_->ReleaseBar0Window();
  }
}

zx_status_t PcieBuscore::PcieRegisterWindow::Read(uint32_t offset, uint32_t* value) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  if (offset + sizeof(uint32_t) > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *value = reinterpret_cast<volatile const std::atomic<uint32_t>*>(base_address_ + offset)
               ->load(std::memory_order::memory_order_relaxed);
  return ZX_OK;
}

zx_status_t PcieBuscore::PcieRegisterWindow::Write(uint32_t offset, uint32_t value) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  if (offset + sizeof(uint32_t) > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  reinterpret_cast<volatile std::atomic<uint32_t>*>(base_address_ + offset)
      ->store(value, std::memory_order::memory_order_relaxed);
  return ZX_OK;
}

zx_status_t PcieBuscore::PcieRegisterWindow::GetRegisterPointer(uint32_t offset, size_t size,
                                                                volatile void** pointer) {
  ZX_DEBUG_ASSERT(parent_ != nullptr);
  if (offset + size > size_) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *pointer = reinterpret_cast<volatile void*>(base_address_ + offset);
  return ZX_OK;
}

PcieBuscore::PcieBuscore() = default;

PcieBuscore::~PcieBuscore() {
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
    BRCMF_ERR("ddk::PciProtocolClient::CreateFromDevice() failed: %s",
              zx_status_get_string(status));
    return status;
  }
  if ((status = pci_proto->EnableBusMaster(true)) != ZX_OK) {
    BRCMF_ERR("Failed to enable PCIE bus provider: %s", zx_status_get_string(status));
    return status;
  }
  zx_pci_bar_t bar0_info = {};
  if ((status = pci_proto->GetBar(0, &bar0_info)) != ZX_OK) {
    BRCMF_ERR("Failed to get BAR0: %s", zx_status_get_string(status));
    return status;
  }
  zx_pci_bar_t bar2_info = {};
  if ((status = pci_proto->GetBar(2, &bar2_info)) != ZX_OK) {
    BRCMF_ERR("Failed to get BAR2: %s", zx_status_get_string(status));
    return status;
  }
  if (bar2_info.size == 0 || bar2_info.handle == ZX_HANDLE_INVALID) {
    BRCMF_ERR("BAR2 invalid: size=%zu, handle=%u", bar2_info.size, bar2_info.handle);
    return ZX_ERR_NO_RESOURCES;
  }

  // Map the MMIO regions.
  std::unique_ptr<ddk::MmioBuffer> regs_mmio;
  {
    size_t vmo_size = 0;
    if ((status = zx_vmo_get_size(bar0_info.handle, &vmo_size)) != ZX_OK) {
      BRCMF_ERR("Failed to get BAR0 VMO size: %s", zx_status_get_string(status));
      return status;
    }
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = ddk::MmioBuffer::Create(0, vmo_size, zx::vmo(bar0_info.handle),
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio)) != ZX_OK) {
      BRCMF_ERR("Failed to create BAR0 MmioBuffer: %s", zx_status_get_string(status));
      return status;
    }
    if (mmio->get_size() != BRCMF_PCIE_REG_MAP_SIZE) {
      BRCMF_ERR("BAR0 mapped size=%zu, expected %u", mmio->get_size(), BRCMF_PCIE_REG_MAP_SIZE);
      return ZX_ERR_NO_RESOURCES;
    }
    regs_mmio = std::make_unique<ddk::MmioBuffer>(std::move(mmio.value()));
  }
  std::unique_ptr<ddk::MmioBuffer> tcm_mmio;
  {
    size_t vmo_size = 0;
    if ((status = zx_vmo_get_size(bar2_info.handle, &vmo_size)) != ZX_OK) {
      BRCMF_ERR("Failed to get BAR2 VMO size: %s", zx_status_get_string(status));
      return status;
    }
    std::optional<ddk::MmioBuffer> mmio;
    if ((status = ddk::MmioBuffer::Create(0, vmo_size, zx::vmo(bar2_info.handle),
                                          ZX_CACHE_POLICY_UNCACHED_DEVICE, &mmio)) != ZX_OK) {
      BRCMF_ERR("Failed to create BAR2 MmioBuffer: %s", zx_status_get_string(status));
      return status;
    }
    tcm_mmio = std::make_unique<ddk::MmioBuffer>(std::move(mmio.value()));
  }

  auto buscore = std::make_unique<PcieBuscore>();
  buscore->pci_proto_ = std::move(pci_proto);
  buscore->regs_mmio_ = std::move(regs_mmio);
  buscore->tcm_mmio_ = std::move(tcm_mmio);

  // Set up the Backplane instance.
  std::unique_ptr<Backplane> backplane;
  if ((status = Backplane::Create(buscore.get(), &backplane)) != ZX_OK) {
    BRCMF_ERR("Failed to create backplane: %s", zx_status_get_string(status));
    return status;
  }
  buscore->backplane_ = std::move(backplane);

  // The BAR2 window may not be sized properly, so we re-write it to confirm.
  {
    PcieRegisterWindow window;
    if ((status = buscore->GetCoreWindow(Backplane::CoreId::kPcie2Core, &window)) != ZX_OK) {
      BRCMF_ERR("Failed to get PCIE2 core window: %s", zx_status_get_string(status));
      return status;
    }

    status = [&]() {
      zx_status_t status = ZX_OK;
      if ((status = window.Write(BRCMF_PCIE_PCIE2REG_CONFIGADDR,
                                 BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG)) != ZX_OK) {
        return status;
      }
      uint32_t bar2_config = 0;
      if ((status = window.Read(BRCMF_PCIE_PCIE2REG_CONFIGDATA, &bar2_config)) != ZX_OK) {
        return status;
      }
      if ((status = window.Write(BRCMF_PCIE_PCIE2REG_CONFIGDATA, bar2_config)) != ZX_OK) {
        return status;
      }
      return ZX_OK;
    }();
    if (status != ZX_OK) {
      BRCMF_ERR("Failed to reset BAR2 window: %s", zx_status_get_string(status));
      return status;
    }
  }

  // Disable ASPM.
  const uint32_t lsc = buscore->ConfigRead(BRCMF_PCIE_REG_LINK_STATUS_CTRL);
  const uint32_t new_lsc = lsc & (~BRCMF_PCIE_LINK_STATUS_CTRL_ASPM_ENAB);
  buscore->ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, new_lsc);

  // Watchdog reset.
  {
    PcieRegisterWindow window;
    if ((status = buscore->GetCoreWindow(Backplane::CoreId::kChipCommonCore, &window)) != ZX_OK) {
      BRCMF_ERR("Failed to get chip common core window: %s", zx_status_get_string(status));
      return status;
    }
    if ((status = window.Write(offsetof(ChipsetCoreRegs, watchdog), 4)) != ZX_OK) {
      BRCMF_ERR("Failed to set chip common watchdog: %s", zx_status_get_string(status));
      return status;
    }
    zx::nanosleep(zx::deadline_after(kChipCommonWatchdogWait));
  }

  // Restore ASPM.
  buscore->ConfigWrite(BRCMF_PCIE_REG_LINK_STATUS_CTRL, lsc);

  {
    const auto core = buscore->backplane_->GetCore(Backplane::CoreId::kPcie2Core);
    if (core == nullptr) {
      BRCMF_ERR("Failed to get PCIE2 core");
      return ZX_ERR_NOT_FOUND;
    }
    PcieRegisterWindow window;
    if ((status = buscore->GetCoreWindow(Backplane::CoreId::kPcie2Core, &window)) != ZX_OK) {
      BRCMF_ERR("Failed to get PCIE2 core window: %s", zx_status_get_string(status));
      return status;
    }

    if (core->rev <= 13) {
      // Re-write these PCIE configuration registers, for unknown (but apparently important)
      // reasons.
      static constexpr uint16_t kCfgOffsets[] = {
          BRCMF_PCIE_CFGREG_STATUS_CMD,        BRCMF_PCIE_CFGREG_PM_CSR,
          BRCMF_PCIE_CFGREG_MSI_CAP,           BRCMF_PCIE_CFGREG_MSI_ADDR_L,
          BRCMF_PCIE_CFGREG_MSI_ADDR_H,        BRCMF_PCIE_CFGREG_MSI_DATA,
          BRCMF_PCIE_CFGREG_LINK_STATUS_CTRL2, BRCMF_PCIE_CFGREG_RBAR_CTRL,
          BRCMF_PCIE_CFGREG_PML1_SUB_CTRL1,    BRCMF_PCIE_CFGREG_REG_BAR2_CONFIG,
          BRCMF_PCIE_CFGREG_REG_BAR3_CONFIG};
      for (const uint16_t offset : kCfgOffsets) {
        status = [&]() {
          zx_status_t status = ZX_OK;
          if ((status = window.Write(BRCMF_PCIE_PCIE2REG_CONFIGADDR, offset)) != ZX_OK) {
            return status;
          }
          uint32_t value = 0;
          if ((status = window.Read(BRCMF_PCIE_PCIE2REG_CONFIGDATA, &value)) != ZX_OK) {
            return status;
          }
          BRCMF_DBG(PCIE, "Buscore device reset: config offset=0x%04x, value=0x%04x", offset,
                    value);
          if ((status = window.Write(BRCMF_PCIE_PCIE2REG_CONFIGDATA, value)) != ZX_OK) {
            return status;
          }
          return ZX_OK;
        }();
        if (status != ZX_OK) {
          BRCMF_ERR("Failed to rewrite PCIE config register: %s", zx_status_get_string(status));
          return status;
        }
      }
    }

    status = [&]() {
      zx_status_t status = ZX_OK;
      uint32_t value = 0;
      if ((status = window.Read(BRCMF_PCIE_PCIE2REG_MAILBOXINT, &value)) != ZX_OK) {
        return status;
      }
      if (value != 0xFFFFFFFF) {
        if ((status = window.Write(BRCMF_PCIE_PCIE2REG_MAILBOXINT, value)) != ZX_OK) {
          return status;
        }
      }
      return ZX_OK;
    }();
    if (status != ZX_OK) {
      BRCMF_ERR("Failed to reset PCIE mailbox: %s", zx_status_get_string(status));
      return status;
    }
  }

  *out_buscore = std::move(buscore);
  return ZX_OK;
}

template <>
zx_status_t PcieBuscore::TcmRead<uint64_t>(uint32_t offset, uint64_t* value) {
  if (offset + sizeof(uint64_t) > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const auto address = reinterpret_cast<volatile const std::atomic<uint32_t>*>(
      reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  const uint32_t value_lo = address[0].load(std::memory_order::memory_order_relaxed);
  const uint64_t value_hi = address[1].load(std::memory_order::memory_order_relaxed);
  *value = (value_hi << 32) | value_lo;
  return ZX_OK;
}

template <>
zx_status_t PcieBuscore::TcmWrite<uint64_t>(uint32_t offset, uint64_t value) {
  if (offset + sizeof(uint64_t) > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  const auto address = reinterpret_cast<volatile std::atomic<uint32_t>*>(
      reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  const uint32_t value_lo = static_cast<uint32_t>(value & 0xFFFFFFFF);
  const uint32_t value_hi = static_cast<uint32_t>(value >> 32);
  address[0].store(value_lo, std::memory_order::memory_order_relaxed);
  address[1].store(value_hi, std::memory_order::memory_order_relaxed);
  return ZX_OK;
}

zx_status_t PcieBuscore::TcmRead(uint32_t offset, void* data, size_t size) {
  if (offset + size > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  auto src = reinterpret_cast<volatile const void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) +
                                                    offset);
  volatile_memcpy(data, src, size);
  return ZX_OK;
}

zx_status_t PcieBuscore::TcmWrite(uint32_t offset, const void* data, size_t size) {
  if (offset + size > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  auto dst =
      reinterpret_cast<volatile void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  volatile_memcpy(dst, data, size);
  return ZX_OK;
}

zx_status_t PcieBuscore::GetTcmPointer(uint32_t offset, size_t size, volatile void** pointer) {
  if (offset + size > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *pointer =
      reinterpret_cast<volatile void*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset);
  return ZX_OK;
}

Backplane* PcieBuscore::GetBackplane() { return backplane_.get(); }

zx_status_t PcieBuscore::GetCoreWindow(Backplane::CoreId core_id,
                                       PcieRegisterWindow* out_pcie_register_window) {
  zx_status_t status = ZX_OK;

  const auto core = backplane_->GetCore(core_id);
  if (core == nullptr) {
    BRCMF_ERR("Failed to get backplane core %d", static_cast<int>(core_id));
    return ZX_ERR_NOT_FOUND;
  }

  if ((status = GetRegisterWindow(core->regbase, core->regsize, out_pcie_register_window)) !=
      ZX_OK) {
    BRCMF_ERR("Failed to get backplane core %d window: %s", static_cast<int>(core_id),
              zx_status_get_string(status));
    return status;
  }

  return ZX_OK;
}

zx_status_t PcieBuscore::GetRegisterWindow(
    uint32_t offset, size_t size,
    std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>* out_register_window) {
  zx_status_t status = ZX_OK;

  // Forward this call to the one that returns a PcieRegisterWindow.
  auto pcie_register_window = std::make_unique<PcieRegisterWindow>();
  if ((status = GetRegisterWindow(offset, size, pcie_register_window.get())) != ZX_OK) {
    return status;
  }

  *out_register_window = std::move(pcie_register_window);
  return ZX_OK;
}

zx_status_t PcieBuscore::CreateDmaBuffer(uint32_t cache_policy, size_t size,
                                         std::unique_ptr<DmaBuffer>* out_dma_buffer) {
  zx_status_t status = ZX_OK;
  zx::bti bti;
  if ((status = pci_proto_->GetBti(0, &bti)) != ZX_OK) {
    return status;
  }
  return DmaBuffer::Create(&bti, cache_policy, size, out_dma_buffer);
}

uint32_t PcieBuscore::ConfigRead(uint16_t offset) {
  uint32_t value = 0;
  pci_proto_->ConfigRead32(offset, &value);
  return value;
}

void PcieBuscore::ConfigWrite(uint16_t offset, uint32_t value) {
  pci_proto_->ConfigWrite32(offset, value);
}

zx_status_t PcieBuscore::AcquireBar0Window(uint32_t base, size_t size,
                                           uintptr_t* out_window_offset) {
  const uint32_t window_address = (base & ~kBar0WindowAddressMask);
  const uint32_t window_offset = base - window_address;

  if (window_offset + size > BRCMF_PCIE_BAR0_REG_SIZE) {
    BRCMF_ERR("Failed to acquire over-size BAR0 window for base 0x%08x size 0x%zx", base, size);
    return ZX_ERR_NO_RESOURCES;
  }

  std::lock_guard lock(bar0_window_mutex_);
  if (window_address != bar0_window_address_) {
    if (bar0_window_refcount_ > 0) {
      BRCMF_ERR("Failed to acquire BAR0 window for base 0x%08x size 0x%zx", base, size);
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
  *out_window_offset = window_offset;
  return ZX_OK;
}

void PcieBuscore::ReleaseBar0Window() {
  std::lock_guard lock(bar0_window_mutex_);
  --bar0_window_refcount_;
}

zx_status_t PcieBuscore::GetRegisterWindow(uint32_t offset, size_t size,
                                           PcieRegisterWindow* out_pcie_register_window) {
  zx_status_t status = ZX_OK;

  uintptr_t window_offset;
  if ((status = AcquireBar0Window(offset, size, &window_offset)) != ZX_OK) {
    BRCMF_ERR("Failed to acquire BAR0 window: %s", zx_status_get_string(status));
    return status;
  }

  *out_pcie_register_window = PcieRegisterWindow(
      this, reinterpret_cast<uintptr_t>(regs_mmio_->get()) + window_offset, size);
  return ZX_OK;
}

}  // namespace brcmfmac
}  // namespace wlan
