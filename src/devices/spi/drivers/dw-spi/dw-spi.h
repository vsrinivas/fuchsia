// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mmio/mmio.h>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/spiimpl.h>
#include <fbl/vector.h>

namespace spi {

class DwSpi;
using DeviceType = ddk::Device<DwSpi, ddk::Unbindable>;

class DwSpi : public DeviceType, public ddk::SpiImplProtocol<DwSpi, ddk::base_protocol> {
 public:
  // Spawns device node.
  static zx_status_t Create(void* ctx, zx_device_t* parent);

  // Device protocol implementation.
  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  uint32_t SpiImplGetChipSelectCount() { return 4; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

 private:
  explicit DwSpi(zx_device_t* device, ddk::MmioBuffer mmio)
      : DeviceType(device), mmio_(std::move(mmio)) {}

  ddk::MmioBuffer mmio_;
};

}  // namespace spi
