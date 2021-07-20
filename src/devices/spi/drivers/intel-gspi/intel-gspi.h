// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_INTEL_GSPI_H_
#define SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_INTEL_GSPI_H_

#include <fuchsia/hardware/spi/llcpp/fidl.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/device-protocol/pci.h>
#include <lib/inspect/cpp/inspect.h>

#include <thread>

#include <ddktl/device.h>

#include "src/devices/lib/acpi/client.h"
#include "src/devices/lib/mmio/include/lib/mmio/mmio.h"
#include "src/devices/spi/drivers/intel-gspi/registers.h"

#define GSPI_CS_COUNT 2

namespace gspi {

class GspiDevice;
using DeviceType = ddk::Device<GspiDevice, ddk::Initializable, ddk::Unbindable>;

// Actual virtio console implementation
class GspiDevice : public DeviceType, public ddk::SpiImplProtocol<GspiDevice, ddk::base_protocol> {
 public:
  GspiDevice(zx_device_t* device, ddk::MmioBuffer mmio, zx::interrupt interrupt, acpi::Client acpi,
             zx::duration irq_timeout = kIrqTimeout)
      : DeviceType(device),
        pci_(device, "pci"),
        mmio_(std::move(mmio)),
        irq_(std::move(interrupt)),
        acpi_(std::move(acpi)),
        irq_timeout_(irq_timeout) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  zx_status_t Bind(std::unique_ptr<GspiDevice>* device_ptr);

  void DdkInit(ddk::InitTxn txn);
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  uint32_t SpiImplGetChipSelectCount() { return GSPI_CS_COUNT; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual)
      __TA_EXCLUDES(lock_);

  zx_status_t SpiImplRegisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo vmo,
                                 uint64_t offset, uint64_t size, uint32_t rights);
  zx_status_t SpiImplUnregisterVmo(uint32_t chip_select, uint32_t vmo_id, zx::vmo* out_vmo);
  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size);
  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size);
  zx_status_t SpiImplExchangeVmo(uint32_t chip_select, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size);

  zx_status_t SpiImplLockBus(uint32_t chip_select) __TA_EXCLUDES(lock_);
  zx_status_t SpiImplUnlockBus(uint32_t chip_select) __TA_EXCLUDES(lock_);

 private:
  static constexpr zx::duration kIrqTimeout = zx::msec(100);  // Picked arbitrarily.
  void IrqThread();
  zx_status_t WaitForFifoService(bool rx);

  // Select the given chip.
  zx_status_t SetChipSelect(uint32_t cs) __TA_REQUIRES(lock_);
  // Select no chips.
  void DeassertChipSelect() __TA_REQUIRES(lock_);

  zx_status_t ValidateChildConfig(Con1Reg& con1);

  ddk::Pci pci_;
  std::mutex lock_;
  ddk::MmioBuffer mmio_;
  zx::interrupt irq_;
  acpi::Client acpi_;
  std::thread irq_thread_;
  inspect::Inspector inspect_;
  std::optional<uint32_t> locked_cs_ __TA_GUARDED(lock_) = std::nullopt;

  // Signalled by IRQ thread to tell main thread that the controller is ready for TX/RX.
  sync_completion_t ready_for_rx_;
  sync_completion_t ready_for_tx_;

  zx::duration irq_timeout_;
};

}  // namespace gspi

#endif  // SRC_DEVICES_SPI_DRIVERS_INTEL_GSPI_INTEL_GSPI_H_
