// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_
#define SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_

#include <lib/mmio/mmio.h>

#include <ddk/protocol/platform/device.h>
#include <ddktl/device.h>
#include <ddktl/protocol/spiimpl.h>
#include <fbl/span.h>

namespace spi {

class MtkSpi;
using DeviceType = ddk::Device<MtkSpi, ddk::UnbindableNew>;

class MtkSpi : public DeviceType, public ddk::SpiImplProtocol<MtkSpi, ddk::base_protocol> {
 public:
  explicit MtkSpi(zx_device_t* device, ddk::MmioBuffer mmio)
      : DeviceType(device), mmio_(std::move(mmio)) {}

  static zx_status_t Create(void* ctx, zx_device_t* device);

  void DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }
  void DdkRelease() { delete this; }

  uint32_t SpiImplGetChipSelectCount() { return 1; }
  zx_status_t SpiImplExchange(uint32_t cs, const uint8_t* txdata, size_t txdata_size,
                              uint8_t* out_rxdata, size_t rxdata_size, size_t* out_rxdata_actual);

 private:
  friend class FakeMtkSpi;

  zx_status_t Init();
  zx_status_t FifoExchange(const uint8_t* txdata, uint8_t* out_rxdata,
                           size_t data_size /* bytes */);
  void FifoTransferPacket(const uint8_t** tx, uint8_t** rx, size_t packet_size);

  ddk::MmioBuffer mmio_;

  // TODO: find correct values
  uint32_t spi_clk_hz_ = 109'000'000;
  uint32_t speed_hz_ = 3'120'000;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_MTK_SPI_MTK_SPI_H_
