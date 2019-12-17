// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_

#include <lib/mmio/mmio.h>
#include <zircon/assert.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <ddk/device.h>
#include <ddktl/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"

namespace wlan {
namespace brcmfmac {

class DmaBuffer;

// This class implements the brcmfmac buscore functionality (see: chip.h) for the PCIE bus.  It
// implements the C-style bus transaction logic as defined by brcmf_buscore_ops, used to perform
// reads and writes over the PCIE bus.
class PcieBuscore {
 public:
  // This class represents a view into a particular core's register space.  Core register access
  // requires setting the BAR0 window mapping, which is global state on the device.  Since this
  // access can be required from multiple places (and possibly concurrently, e.g. worker threads and
  // interrupt handlers), it must be access-protected.
  //
  // At a first level, the PcieBuscore instance internally provides AcquireBar0Window() and
  // ReleaseBar0Window() to mediate access to this window, allowing multiple users to concurrently
  // share the window if they request the same window setting.  For the external interface, this
  // window is held by a CoreRegs instance in an RAII fashion, thus turning a possibly brittle
  // threading problem into a more tractable ownership problem; as long as a CoreRegs instance is
  // valid, register access through that instance is valid.
  class CoreRegs {
   public:
    CoreRegs();
    CoreRegs(const CoreRegs& other) = delete;
    CoreRegs(CoreRegs&& other);
    CoreRegs& operator=(CoreRegs other);
    friend void swap(CoreRegs& lhs, CoreRegs& rhs);
    ~CoreRegs();

    // State accessors.
    bool is_valid() const;

    // Read/write registers to the core.
    uint32_t RegRead(uint32_t offset);
    void RegWrite(uint32_t offset, uint32_t value);

    // Get a pointer to the core's register space.  This pointer is valid as long as the CoreRegs
    // instance is valid.
    volatile void* GetRegPointer(uint32_t offset);

   private:
    friend class PcieBuscore;

    explicit CoreRegs(PcieBuscore* parent, uint32_t base_address);

    PcieBuscore* parent_ = nullptr;
    uintptr_t regs_offset_ = 0;
  };

  PcieBuscore();
  ~PcieBuscore();

  // Static factory function for PcieBuscore instances.
  static zx_status_t Create(zx_device_t* device, std::unique_ptr<PcieBuscore>* out_buscore);

  // Get a CoreRegs instance for reading/writing from/to a particular core.  This may fail if
  // another instance already exists with an incompatible BAR0 window mapping.
  zx_status_t GetCoreRegs(uint16_t coreid, CoreRegs* out_core_regs);

  // Read/write registers through the PCIE bus.
  template <typename T>
  T TcmRead(uint32_t offset);
  template <typename T>
  void TcmWrite(uint32_t offset, T value);

  // Read/write memory regions through the PCIE bus.
  void TcmRead(uint32_t offset, void* data, size_t size);
  void TcmWrite(uint32_t offset, const void* data, size_t size);
  void RamRead(uint32_t offset, void* data, size_t size);
  void RamWrite(uint32_t offset, const void* data, size_t size);

  // Get a pointer to the device shared memory region.
  volatile void* GetTcmPointer(uint32_t offset);

  // Create a DMA buffer, suitable for use with the device.
  zx_status_t CreateDmaBuffer(uint32_t cache_policy, size_t size,
                              std::unique_ptr<DmaBuffer>* out_dma_buffer);

  // Manually set the ramsize for this PCIE buscore chip.
  void SetRamsize(size_t ramsize);

  // Data accessors.
  brcmf_chip* chip();
  const brcmf_chip* chip() const;

  // Get the brcmf_buscore_ops struct that forwards brcmf buscore ops to a PcieBuscore instance.
  static const brcmf_buscore_ops* GetBuscoreOps();

 private:
  // PCIE config read/write.
  uint32_t ConfigRead(uint32_t offset);
  void ConfigWrite(uint32_t offset, uint32_t value);

  // Configure the BAR0 window mapping.
  zx_status_t AcquireBar0Window(uint32_t address);
  void ReleaseBar0Window();

  // Buscore brcmf_buscore_ops functionality implementation.
  uint32_t OpRead32(uint32_t address);
  void OpWrite32(uint32_t address, uint32_t value);
  zx_status_t OpPrepare();
  int OpReset(brcmf_chip* chip);
  void OpActivate(brcmf_chip* chip, uint32_t rstvec);

  // Reset the buscore.
  zx_status_t ResetDevice(brcmf_chip* chip);

  // Data members.
  std::unique_ptr<ddk::PciProtocolClient> pci_proto_;
  std::unique_ptr<ddk::MmioBuffer> regs_mmio_;
  std::unique_ptr<ddk::MmioBuffer> tcm_mmio_;
  brcmf_chip* chip_ = nullptr;

  // BAR0 window mapping state.
  std::mutex bar0_window_mutex_;
  uint32_t bar0_window_address_ = ~0u;
  int bar0_window_refcount_ = 0;
};

template <typename T>
T PcieBuscore::TcmRead(uint32_t offset) {
  ZX_DEBUG_ASSERT(offset + sizeof(T) <= tcm_mmio_->get_size());
  return reinterpret_cast<const volatile std::atomic<T>*>(
             reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
      ->load(std::memory_order::memory_order_relaxed);
}

template <typename T>
void PcieBuscore::TcmWrite(uint32_t offset, T value) {
  ZX_DEBUG_ASSERT(offset + sizeof(T) <= tcm_mmio_->get_size());
  reinterpret_cast<volatile std::atomic<T>*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
      ->store(value, std::memory_order::memory_order_relaxed);
}

// Specializations for 64-bit accesses, which have to be broken down to 32-bit accesses.

template <>
uint64_t PcieBuscore::TcmRead(uint32_t offset);

template <>
void PcieBuscore::TcmWrite<uint64_t>(uint32_t offset, uint64_t value);

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
