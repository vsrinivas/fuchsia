// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/empty-protocol.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "i2c-bus.h"

namespace i2c {

class I2cDevice;
using I2cDeviceType = ddk::Device<I2cDevice, ddk::UnbindableNew>;

class I2cDevice : public I2cDeviceType {
 public:
  I2cDevice(zx_device_t* parent, const i2c_impl_protocol_t* i2c)
      : I2cDeviceType(parent), i2c_(i2c) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

 private:
  zx_status_t Init(ddk::I2cImplProtocolClient i2c);
  void AddChildren();

  const ddk::I2cImplProtocolClient i2c_;
  // List of I2C buses.
  fbl::Vector<fbl::RefPtr<I2cBus>> i2c_buses_;
};

}  // namespace i2c
