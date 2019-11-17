// Copyright (c) 2019 The Fuchsia Authors
//
// Permission to use, copy, modify, and/or distribute this software for any purpose with or without
// fee is hereby granted, provided that the above copyright notice and this permission notice
// appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
// SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
// AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT,
// NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
// OF THIS SOFTWARE.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_

#include <lib/mmio/mmio.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>

#include <ddk/device.h>
#include <ddktl/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chip.h"

namespace wlan {
namespace brcmfmac {

// This class implements the brcmfmac buscore functionality (see: chip.h) for the PCIE bus.  It
// implements the C-style bus transaction logic as defined by brcmf_buscore_ops, used to perform
// reads and writes over the PCIE bus.
class PcieBuscore {
 public:
  PcieBuscore();
  ~PcieBuscore();

  // Static factory function for PcieBuscore instances.
  static zx_status_t Create(zx_device_t* device, std::unique_ptr<PcieBuscore>* out_buscore);

  // Read/write registers through the PCIE bus.
  template <typename T>
  T RegRead(uint32_t offset);
  template <typename T>
  void RegWrite(uint32_t offset, T value);
  template <typename T>
  T TcmRead(uint32_t offset);
  template <typename T>
  void TcmWrite(uint32_t offset, T value);

  // Read/write memory regions through the PCIE bus.
  void TcmRead(uint32_t offset, void* data, size_t size);
  void TcmWrite(uint32_t offset, const void* data, size_t size);
  void RamRead(uint32_t offset, void* data, size_t size);
  void RamWrite(uint32_t offset, const void* data, size_t size);

  // Get a pointer to the PCIE memory regions.
  void* GetRegPointer(uint32_t offset);
  void* GetTcmPointer(uint32_t offset);

  // Select the core to write configuration to.
  void SelectCore(uint16_t coreid);

  // Manually set the ramsize for this PCIE buscore chip.
  void SetRamsize(size_t ramsize);

  // Data accessors.
  brcmf_chip* chip();
  const brcmf_chip* chip() const;

  // Get the brcmf_buscore_ops struct that forwards brcmf buscore ops to a PcieBuscore instance.
  static const brcmf_buscore_ops* GetBuscoreOps();

 private:
  // MMIO read/write.
  uint32_t SetBar0Window(uint32_t address);
  uint32_t ConfigRead(uint32_t offset);
  void ConfigWrite(uint32_t offset, uint32_t value);

  // Buscore brcmf_buscore_ops functionality implementation.
  uint32_t OpRead32(uint32_t address);
  void OpWrite32(uint32_t address, uint32_t value);
  zx_status_t OpPrepare();
  int OpReset(brcmf_chip* chip);
  void OpActivate(brcmf_chip* chip, uint32_t rstvec);

  // Select the core to write configuration to.  Internal version of this call.
  void SelectCore(brcmf_chip* chip, uint16_t coreid);

  // Reset the buscore.
  void ResetDevice(brcmf_chip* chip);

  // Data members.
  std::unique_ptr<ddk::PciProtocolClient> pci_proto_;
  std::unique_ptr<ddk::MmioBuffer> regs_mmio_;
  std::unique_ptr<ddk::MmioBuffer> tcm_mmio_;
  brcmf_chip* chip_ = nullptr;
};

template <typename T>
T PcieBuscore::RegRead(uint32_t offset) {
  return reinterpret_cast<std::atomic<T>*>(reinterpret_cast<uintptr_t>(regs_mmio_->get()) + offset)
      ->load(std::memory_order::memory_order_acquire);
}

template <typename T>
void PcieBuscore::RegWrite(uint32_t offset, T value) {
  reinterpret_cast<std::atomic<T>*>(reinterpret_cast<uintptr_t>(regs_mmio_->get()) + offset)
      ->store(value, std::memory_order::memory_order_release);
}

template <typename T>
T PcieBuscore::TcmRead(uint32_t offset) {
  return reinterpret_cast<std::atomic<T>*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
      ->load(std::memory_order::memory_order_acquire);
}

template <typename T>
void PcieBuscore::TcmWrite(uint32_t offset, T value) {
  reinterpret_cast<std::atomic<T>*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
      ->store(value, std::memory_order::memory_order_release);
}

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
