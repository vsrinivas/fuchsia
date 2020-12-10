// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_

#include <lib/mmio/mmio.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <atomic>
#include <memory>
#include <mutex>

#include <ddk/device.h>
#include <ddktl/protocol/pci.h>

#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/backplane.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/chipset/chipset_interfaces.h"
#include "src/connectivity/wlan/drivers/third_party/broadcom/brcmfmac/msgbuf/msgbuf_interfaces.h"

namespace wlan {
namespace brcmfmac {

class DmaBuffer;

// This class implements the brcmfmac buscore functionality for the PCIE bus.  It provides bus
// access for the rest of the PCIE bus driver layer, as well as a few interfaces used elsewhere:
//
// * RegisterWindowProviderInterface, used by the chipset logic.
// * DmaBufferProviderInterface, used by the MSGBUF logic.
class PcieBuscore : public RegisterWindowProviderInterface, public DmaBufferProviderInterface {
 public:
  // This class represents a view into a particular core's register space.  Core register access
  // requires setting the BAR0 window mapping, which is global state on the device.  Since this
  // access can be required from multiple places (and possibly concurrently, e.g. worker threads and
  // interrupt handlers), it must be access-protected.
  //
  // At a first level, the PcieBuscore instance internally provides AcquireBar0Window() and
  // ReleaseBar0Window() to mediate access to this window, allowing multiple users to concurrently
  // share the window if they request the same window setting.  For the external interface, this
  // window is held by a RegisterWindow instance in an RAII fashion, thus turning a possibly brittle
  // threading problem into a more tractable ownership problem; as long as a RegisterWindow instance
  // is valid, register access through that instance is valid.
  class PcieRegisterWindow : public RegisterWindowProviderInterface::RegisterWindow {
   public:
    PcieRegisterWindow();
    PcieRegisterWindow(PcieRegisterWindow&& other);
    PcieRegisterWindow& operator=(PcieRegisterWindow other);
    friend void swap(PcieRegisterWindow& lhs, PcieRegisterWindow& rhs);
    ~PcieRegisterWindow() override;

    // Get a pointer to the core's register space.  This pointer is valid as long as the
    // PcieRegisterWindow instance is valid.
    zx_status_t GetRegisterPointer(uint32_t offset, size_t size, volatile void** pointer);

    // RegisterWindowProviderInterface::RegisterWindow implementation.
    zx_status_t Read(uint32_t offset, uint32_t* value) override;
    zx_status_t Write(uint32_t offset, uint32_t value) override;

   private:
    friend class PcieBuscore;

    explicit PcieRegisterWindow(PcieBuscore* parent, uintptr_t base_address, size_t size);

    PcieBuscore* parent_ = nullptr;
    uintptr_t base_address_ = 0;
    size_t size_ = 0;
  };

  PcieBuscore();
  ~PcieBuscore() override;

  // Static factory function for PcieBuscore instances.
  static zx_status_t Create(zx_device_t* device, std::unique_ptr<PcieBuscore>* out_buscore);

  // Read/write registers through the PCIE bus.
  template <typename T>
  zx_status_t TcmRead(uint32_t offset, T* value);
  template <typename T>
  zx_status_t TcmWrite(uint32_t offset, T value);

  // Read/write memory regions through the PCIE bus.
  zx_status_t TcmRead(uint32_t offset, void* data, size_t size);
  zx_status_t TcmWrite(uint32_t offset, const void* data, size_t size);

  // Get a pointer to the device shared memory region.
  zx_status_t GetTcmPointer(uint32_t offset, size_t size, volatile void** pointer);

  // DmaBufferProviderInterface implementation.
  zx_status_t CreateDmaBuffer(uint32_t cache_policy, size_t size,
                              std::unique_ptr<DmaBuffer>* out_dma_buffer) override;

  // Get a pointer to the Backplane instance.
  Backplane* GetBackplane();

  // Get a PcieRegisterWindow instance for access to a particular core's register space.  This call
  // may fail if another instance already exists with an incompatible BAR0 window mapping.
  zx_status_t GetCoreWindow(Backplane::CoreId core_id, PcieRegisterWindow* out_register_window);

  // RegisterWindowProviderInterface implementation.
  zx_status_t GetRegisterWindow(uint32_t offset, size_t size,
                                std::unique_ptr<RegisterWindowProviderInterface::RegisterWindow>*
                                    out_register_window) override;

 private:
  // PCIE config read/write.
  uint32_t ConfigRead(uint16_t offset);
  void ConfigWrite(uint16_t offset, uint32_t value);

  // Configure the BAR0 window mapping.
  zx_status_t AcquireBar0Window(uint32_t address, size_t size, uintptr_t* out_window_offset);
  void ReleaseBar0Window();

  // Get a PcieRegisterWindow instance for a particular register window.
  zx_status_t GetRegisterWindow(uint32_t offset, size_t size,
                                PcieRegisterWindow* out_register_window);

  // Data members.
  std::unique_ptr<ddk::PciProtocolClient> pci_proto_;
  std::unique_ptr<ddk::MmioBuffer> regs_mmio_;
  std::unique_ptr<ddk::MmioBuffer> tcm_mmio_;
  std::unique_ptr<Backplane> backplane_;

  // BAR0 window mapping state.
  std::mutex bar0_window_mutex_;
  uint32_t bar0_window_address_ = ~0u;
  int bar0_window_refcount_ = 0;
};

template <typename T>
zx_status_t PcieBuscore::TcmRead(uint32_t offset, T* value) {
  if (offset + sizeof(T) > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  *value = reinterpret_cast<const volatile std::atomic<T>*>(
               reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
               ->load(std::memory_order::memory_order_relaxed);
  return ZX_OK;
}

template <typename T>
zx_status_t PcieBuscore::TcmWrite(uint32_t offset, T value) {
  if (offset + sizeof(T) > tcm_mmio_->get_size()) {
    return ZX_ERR_OUT_OF_RANGE;
  }
  reinterpret_cast<volatile std::atomic<T>*>(reinterpret_cast<uintptr_t>(tcm_mmio_->get()) + offset)
      ->store(value, std::memory_order::memory_order_relaxed);
  return ZX_OK;
}

// Specializations for 64-bit accesses, which have to be broken down to 32-bit accesses.

template <>
zx_status_t PcieBuscore::TcmRead(uint32_t offset, uint64_t* value);

template <>
zx_status_t PcieBuscore::TcmWrite<uint64_t>(uint32_t offset, uint64_t value);

}  // namespace brcmfmac
}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_BROADCOM_BRCMFMAC_PCIE_PCIE_BUSCORE_H_
