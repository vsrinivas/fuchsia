// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_DRIVERS_I2C_I2C_BUS_H_
#define SRC_DEVICES_I2C_DRIVERS_I2C_I2C_BUS_H_

#include <lib/sync/completion.h>
#include <threads.h>

#include <ddk/driver.h>
#include <ddktl/protocol/i2c.h>
#include <ddktl/protocol/i2cimpl.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>

namespace i2c {

class I2cBus : public fbl::RefCounted<I2cBus> {
 public:
  explicit I2cBus(zx_device_t* parent, ddk::I2cImplProtocolClient i2c, uint32_t bus_id);
  virtual ~I2cBus() = default;
  zx_status_t Start();
  virtual void Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                        i2c_transact_callback callback, void* cookie);

  size_t max_transfer() const { return max_transfer_; }

 private:
  // struct representing an I2C transaction.
  struct I2cTxn {
    list_node_t node;
    uint16_t address;
    i2c_transact_callback transact_cb;
    void* cookie;
    size_t length;
    size_t op_count;
  };

  int I2cThread();

  zx_device_t* parent_;
  ddk::I2cImplProtocolClient i2c_;
  const uint32_t bus_id_;
  size_t max_transfer_;

  list_node_t queued_txns_ __TA_GUARDED(mutex_);
  list_node_t free_txns_ __TA_GUARDED(mutex_);
  sync_completion_t txn_signal_;

  thrd_t thread_;
  fbl::Mutex mutex_;
};

}  // namespace i2c

#endif  // SRC_DEVICES_I2C_DRIVERS_I2C_I2C_BUS_H_
