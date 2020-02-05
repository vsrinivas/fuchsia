// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <fbl/alloc_checker.h>
#include <fbl/mutex.h>
#include <lib/sync/completion.h>
#include <zircon/types.h>

#include "i2c-child.h"

namespace i2c {

void I2cChild::I2cTransact(const i2c_op_t* op_list, size_t op_count, i2c_transact_callback callback,
                           void* cookie) {
  bus_->Transact(address_, op_list, op_count, callback, cookie);
}

zx_status_t I2cChild::I2cGetMaxTransferSize(size_t* out_size) {
  *out_size = bus_->max_transfer();
  return ZX_OK;
}

zx_status_t I2cChild::I2cGetInterrupt(uint32_t flags, zx::interrupt* out_irq) {
  // This is only used by the Intel I2C driver, which does not implement I2C_IMPL
  return ZX_ERR_NOT_SUPPORTED;
}

void I2cChild::DdkUnbindNew(ddk::UnbindTxn txn) { txn.Reply(); }

void I2cChild::DdkRelease() { delete this; }

}  // namespace i2c
