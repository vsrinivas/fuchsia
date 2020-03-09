// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_
#define SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_

#include <lib/mmio/mmio.h>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/spiimpl.h>

namespace spi {

class MtkSpi;
using DeviceType = ddk::Device<MtkSpi, ddk::UnbindableNew>;

class MtkSpi : public DeviceType, public ddk::SpiImplProtocol<MtkSpi, ddk::base_protocol> {
 public:
  static zx_status_t Create(void* ctx, zx_device_t* device);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  uint32_t SpiImplGetChipSelectCount() { return 0; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

 private:
  explicit MtkSpi(zx_device_t* device, ddk::MmioBuffer mmio)
      : DeviceType(device), mmio_(std::move(mmio)) {}

  ddk::MmioBuffer mmio_;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_
