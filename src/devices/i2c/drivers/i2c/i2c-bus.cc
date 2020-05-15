// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-bus.h"

#include <lib/device-protocol/i2c.h>
#include <lib/zx/profile.h>
#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/assert.h>
#include <zircon/listnode.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/threads.h>

#include <mutex>

#include <ddk/debug.h>
#include <ddk/trace/event.h>
#include <fbl/alloc_checker.h>
#include <fbl/array.h>
#include <fbl/auto_lock.h>

namespace i2c {

I2cBus::I2cBus(zx_device_t* parent, ddk::I2cImplProtocolClient i2c, uint32_t bus_id)
    : parent_(parent), i2c_(i2c), bus_id_(bus_id) {
  list_initialize(&queued_txns_);
  list_initialize(&free_txns_);
  sync_completion_reset(&txn_signal_);
}

zx_status_t I2cBus::Start() {
  auto status = i2c_.GetMaxTransferSize(bus_id_, &max_transfer_);
  if (status != ZX_OK) {
    return status;
  }

  char name[32];
  snprintf(name, sizeof(name), "I2cBus[%u]", bus_id_);
  auto thunk = [](void* arg) -> int { return static_cast<I2cBus*>(arg)->I2cThread(); };
  thrd_create_with_name(&thread_, thunk, this, name);

  // Set profile for bus transaction thread.
  // TODO(40858): Migrate to the role-based API when available, instead of hard
  // coding parameters.
  {
    const zx::duration capacity = zx::usec(100);
    const zx::duration deadline = zx::msec(1);
    const zx::duration period = deadline;

    zx::profile bus_profile;
    status = device_get_deadline_profile(parent_, capacity.get(), deadline.get(), period.get(),
                                         name, bus_profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARN, "I2cBus::Start: Failed to get deadline profile: %s",
             zx_status_get_string(status));
    } else {
      status = zx_object_set_profile(thrd_get_zx_handle(thread_), bus_profile.get(), 0);
      if (status != ZX_OK) {
        zxlogf(WARN, "I2cBus::Start: Failed to apply deadline profile to bus thread: %s",
               zx_status_get_string(status));
      }
    }
  }

  return ZX_OK;
}

int I2cBus::I2cThread() {
  fbl::AllocChecker ac;
  fbl::Array<uint8_t> read_buffer(new (&ac) uint8_t[I2C_MAX_TOTAL_TRANSFER],
                                  I2C_MAX_TOTAL_TRANSFER);
  if (!ac.check()) {
    zxlogf(ERROR, "%s could not allocate read_buffer", __FUNCTION__);
    return 0;
  }

  while (1) {
    sync_completion_wait(&txn_signal_, ZX_TIME_INFINITE);
    sync_completion_reset(&txn_signal_);
    I2cTxn* txn;

    mutex_.Acquire();
    while ((txn = list_remove_head_type(&queued_txns_, I2cTxn, node)) != nullptr) {
      mutex_.Release();

      TRACE_DURATION("i2c", "I2cBus Process Queued Transacts");
      TRACE_FLOW_END("i2c", "I2cBus Transact Flow", txn->trace_id, "Flow", txn->trace_id);

      auto op_list = reinterpret_cast<i2c_op_t*>(txn + 1);
      auto op_count = txn->op_count;
      auto p_writes = reinterpret_cast<uint8_t*>(op_list) + op_count * sizeof(i2c_op_t);
      uint8_t* p_reads = read_buffer.data();

      ZX_ASSERT(op_count < I2C_MAX_RW_OPS);
      i2c_impl_op_t impl_ops[I2C_MAX_RW_OPS];
      for (size_t i = 0; i < op_count; ++i) {
        // Same address for all ops, since there is one address per channel.
        impl_ops[i].address = txn->address;
        impl_ops[i].data_size = op_list[i].data_size;
        impl_ops[i].is_read = op_list[i].is_read;
        impl_ops[i].stop = op_list[i].stop;
        if (impl_ops[i].is_read) {
          impl_ops[i].data_buffer = p_reads;
          p_reads += impl_ops[i].data_size;
        } else {
          impl_ops[i].data_buffer = p_writes;
          p_writes += impl_ops[i].data_size;
        }
      }
      auto status = i2c_.Transact(bus_id_, impl_ops, op_count);

      if (status == ZX_OK) {
        i2c_op_t read_ops[I2C_MAX_RW_OPS];
        size_t read_ops_cnt = 0;
        for (size_t i = 0; i < op_count; ++i) {
          if (op_list[i].is_read) {
            read_ops[read_ops_cnt] = op_list[i];
            read_ops[read_ops_cnt].data_buffer = impl_ops[i].data_buffer;
            read_ops_cnt++;
          }
        }
        txn->transact_cb(txn->cookie, ZX_OK, read_ops, read_ops_cnt);
      } else {
        txn->transact_cb(txn->cookie, status, nullptr, 0);
      }

      mutex_.Acquire();
      list_add_tail(&free_txns_, &txn->node);
    }
    mutex_.Release();
  }
  return 0;
}

void I2cBus::Transact(uint16_t address, const i2c_op_t* op_list, size_t op_count,
                      i2c_transact_callback callback, void* cookie) {
  TRACE_DURATION("i2c", "I2cBus Queue Transact");

  // TODO(52177): This is a hack to apply a deadline profile to the dispatch
  // thread for this devhost. Replace this with a proper solution.
  static std::once_flag profile_flag;
  std::call_once(profile_flag, [device = parent_] {
    // Set profile for bus transaction thread.
    // TODO(40858): Migrate to the role-based API when available, instead of hard
    // coding parameters.
    const zx::duration capacity = zx::usec(150);
    const zx::duration deadline = zx::msec(1);
    const zx::duration period = deadline;

    zx::profile profile;
    zx_status_t status =
        device_get_deadline_profile(device, capacity.get(), deadline.get(), period.get(),
                                    "I2cBus Dispatcher", profile.reset_and_get_address());
    if (status != ZX_OK) {
      zxlogf(WARN, "I2cBus::Transact: Failed to get deadline profile: %s",
             zx_status_get_string(status));
    } else {
      status = zx::thread::self()->set_profile(profile, 0);
      if (status != ZX_OK) {
        zxlogf(WARN, "I2cBus::Transact: Failed to apply deadline profile to dispatch thread: %s",
               zx_status_get_string(status));
      }
    }
  });

  size_t writes_length = 0;
  for (size_t i = 0; i < op_count; ++i) {
    if (op_list[i].data_size == 0 || op_list[i].data_size > max_transfer_) {
      callback(cookie, ZX_ERR_INVALID_ARGS, nullptr, 0);
      return;
    }
    if (!op_list[i].is_read) {
      writes_length += op_list[i].data_size;
    }
  }
  // Add space for requests and writes data.
  size_t req_length = sizeof(I2cTxn) + op_count * sizeof(i2c_op_t) + writes_length;
  if (req_length >= I2C_MAX_TOTAL_TRANSFER) {
    callback(cookie, ZX_ERR_BUFFER_TOO_SMALL, nullptr, 0);
    return;
  }

  fbl::AutoLock lock(&mutex_);

  I2cTxn* txn = list_remove_head_type(&free_txns_, I2cTxn, node);
  if (txn && txn->length < req_length) {
    free(txn);
    txn = nullptr;
  }

  if (!txn) {
    // add space for write buffer
    txn = static_cast<I2cTxn*>(calloc(1, req_length));
    if (!txn) {
      callback(cookie, ZX_ERR_NO_MEMORY, nullptr, 0);
      return;
    }
    txn->length = req_length;
  }

  txn->address = address;
  txn->op_count = op_count;
  txn->transact_cb = callback;
  txn->cookie = cookie;
  if (TRACE_ENABLED()) {
    txn->trace_id = TRACE_NONCE();
    TRACE_FLOW_BEGIN("i2c", "I2cBus Transact Flow", txn->trace_id, "Flow", txn->trace_id);
  }

  // copy the op_list
  auto* dest = reinterpret_cast<uint8_t*>(txn + 1);
  memcpy(dest, op_list, op_count * sizeof(op_list[0]));
  dest += op_count * sizeof(op_list[0]);
  // copy the write buffers
  for (size_t i = 0; i < op_count; ++i) {
    if (!op_list[i].is_read) {
      memcpy(dest, op_list[i].data_buffer, op_list[i].data_size);
      dest += op_list[i].data_size;
    }
  }

  list_add_tail(&queued_txns_, &txn->node);
  sync_completion_signal(&txn_signal_);
}

}  // namespace i2c
