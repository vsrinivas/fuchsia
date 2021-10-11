// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_
#define SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_

#include <fuchsia/hardware/spiimpl/cpp/banjo.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/zircon-internal/thread_annotations.h>

#include <ddktl/device.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "spi-child.h"

namespace spi {

class SpiDevice;
using SpiDeviceType = ddk::Device<SpiDevice, ddk::Unbindable>;

class SpiDevice : public SpiDeviceType {
 public:
  SpiDevice(zx_device_t* parent, uint32_t bus_id)
      : SpiDeviceType(parent), bus_id_(bus_id), loop_(&kAsyncLoopConfigNeverAttachToThread) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbind(ddk::UnbindTxn txn);
  void DdkRelease();

  void ConnectServer(zx::channel server, fbl::RefPtr<SpiChild> child);

 private:
  void AddChildren(const ddk::SpiImplProtocolClient& spi);
  void Shutdown();

  fbl::Mutex lock_;
  fbl::Vector<fbl::RefPtr<SpiChild>> children_ TA_GUARDED(lock_);  // Must outlive loop_
  const uint32_t bus_id_;
  async::Loop loop_ TA_GUARDED(lock_);
  bool loop_started_ TA_GUARDED(lock_) = false;
  bool shutdown_ TA_GUARDED(lock_) = false;
};

}  // namespace spi

#endif  // SRC_DEVICES_SPI_DRIVERS_SPI_SPI_H_
