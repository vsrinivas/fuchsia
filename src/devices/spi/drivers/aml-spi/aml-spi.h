// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/spiimpl.h>
#include <fbl/vector.h>
#include <soc/aml-common/aml-spi.h>

namespace spi {

class AmlSpi;
using DeviceType = ddk::Device<AmlSpi, ddk::UnbindableNew>;

class AmlSpi : public DeviceType, public ddk::SpiImplProtocol<AmlSpi, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* device);

  // Device protocol implementation.
  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  uint32_t SpiImplGetChipSelectCount() { return static_cast<uint32_t>(gpio_.size()); }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

 private:
  explicit AmlSpi(zx_device_t* device, ddk::MmioBuffer mmio)
      : DeviceType(device), mmio_(std::move(mmio)) {}

  zx_status_t GpioInit(amlspi_cs_map_t* map, zx_device_t** gpio_components, size_t count);
  void DumpState();

  fbl::Vector<ddk::GpioProtocolClient> gpio_;
  ddk::MmioBuffer mmio_;
};

}  // namespace spi
