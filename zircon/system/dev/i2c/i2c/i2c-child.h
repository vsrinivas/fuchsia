// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/platform-defs.h>
#include <ddktl/device.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/i2cimpl.h>
#include <fbl/ref_ptr.h>
#include <fbl/vector.h>

#include "i2c-bus.h"

namespace i2c {

class I2cChild;
using I2cChildType = ddk::Device<I2cChild, ddk::UnbindableNew>;

class I2cChild : public I2cChildType, public ddk::I2cProtocol<I2cChild, ddk::base_protocol> {
 public:
  I2cChild(zx_device_t* parent, ddk::I2cImplProtocolClient i2c, fbl::RefPtr<I2cBus> bus,
           const i2c_channel_t* channel)
      : I2cChildType(parent), bus_(bus), address_(channel->address) {}

  static zx_status_t Create(void* ctx, zx_device_t* parent);

  void DdkUnbindNew(ddk::UnbindTxn txn);
  void DdkRelease();

  void I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                   void* cookie);
  zx_status_t I2cGetMaxTransferSize(size_t* out_size);
  zx_status_t I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq);

 private:
  fbl::RefPtr<I2cBus> bus_;
  const uint16_t address_;
};

}  // namespace i2c
