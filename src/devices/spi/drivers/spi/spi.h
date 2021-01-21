// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_
#define SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_

#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>

#include <ddktl/device.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "spi-child.h"

namespace spi {

class SpiDevice;
using SpiDeviceType = ddk::Device<SpiDevice, ddk::Unbindable>;

class SpiDevice : public SpiDeviceType {
 public:
  SpiDevice(zx_device_t* parent, const spi_impl_protocol_t* spi, uint32_t bus_id)
      : SpiDeviceType(parent),
        spi_(spi),
        bus_id_(bus_id),
        loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void ConnectServer(zx::channel server, SpiChild* ctx);

 private:
  void AddChildren();

  fbl::Vector<fbl::RefPtr<SpiChild>> children_;
  const ddk::SpiImplProtocolClient spi_;
  uint32_t bus_id_;
  async::Loop loop_;
  std::atomic<bool> loop_started_ = false;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_
