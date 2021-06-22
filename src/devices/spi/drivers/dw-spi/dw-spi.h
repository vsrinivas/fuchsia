// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/mmio/mmio.h>

#include <ddktl/device.h>
#include <fbl/vector.h>

namespace spi {

class DwSpi;
using DeviceType = ddk::Device<DwSpi>;

class DwSpi : public DeviceType, public ddk::SpiImplProtocol<DwSpi, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkRelease();

  uint32_t SpiImplGetChipSelectCount() { return 4; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);
  zx_status_t SpiImplRegisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo vmo, uint64_t offset,
                                 uint64_t size, uint32_t rights);
  zx_status_t SpiImplUnregisterVmo(uint32_t cs, uint32_t vmo_id, zx::vmo* out_vmo);
  zx_status_t SpiImplTransmitVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                 uint64_t size);
  zx_status_t SpiImplReceiveVmo(uint32_t chip_select, uint32_t vmo_id, uint64_t offset,
                                uint64_t size);
  zx_status_t SpiImplExchangeVmo(uint32_t cs, uint32_t tx_vmo_id, uint64_t tx_offset,
                                 uint32_t rx_vmo_id, uint64_t rx_offset, uint64_t size);

 private:
  explicit DwSpi(zx_device_t* device, ddk::MmioBuffer mmio)
      : DeviceType(device), mmio_(std::move(mmio)) {}

  ddk::MmioBuffer mmio_;
};

}  // namespace spi
