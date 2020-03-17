// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "i2c-child.h"

#include <lib/sync/completion.h>
#include <threads.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/metadata.h>
#include <ddk/metadata/i2c.h>
#include <fbl/alloc_checker.h>
#include <fbl/mutex.h>

namespace i2c {

void I2cChild::Transfer(fidl::VectorView<bool> segments_is_write,
                        fidl::VectorView<fidl::VectorView<uint8_t>> write_segments_data,
                        fidl::VectorView<uint8_t> read_segments_length,
                        TransferCompleter::Sync completer) {
  if (segments_is_write.count() < 1) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  auto op_list = std::make_unique<i2c_op_t[]>(segments_is_write.count());
  size_t write_cnt = 0;
  size_t read_cnt = 0;
  for (size_t i = 0; i < segments_is_write.count(); ++i) {
    if (segments_is_write[i]) {
      if (write_cnt >= write_segments_data.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].data_buffer = write_segments_data[write_cnt].data();
      op_list[i].data_size = write_segments_data[write_cnt].count();
      op_list[i].is_read = false;
      op_list[i].stop = false;
      write_cnt++;
    } else {
      if (read_cnt >= read_segments_length.count()) {
        completer.ReplyError(ZX_ERR_INVALID_ARGS);
        return;
      }
      op_list[i].data_buffer = nullptr;  // unused.
      op_list[i].data_size = read_segments_length[read_cnt];
      op_list[i].is_read = true;
      op_list[i].stop = false;
      read_cnt++;
    }
  }
  op_list[segments_is_write.count() - 1].stop = true;

  if (write_segments_data.count() != write_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }
  if (read_segments_length.count() != read_cnt) {
    completer.ReplyError(ZX_ERR_INVALID_ARGS);
    return;
  }

  struct Ctx {
    sync_completion_t done = {};
    TransferCompleter::Sync* completer;
  } ctx;
  ctx.completer = &completer;
  auto callback = [](void* ctx, zx_status_t status, const i2c_op_t* op_list, size_t op_count) {
    auto ctx2 = static_cast<Ctx*>(ctx);
    if (status == ZX_OK) {
      auto reads = std::make_unique<fidl::VectorView<uint8_t>[]>(op_count);
      for (size_t i = 0; i < op_count; ++i) {
        reads[i].set_data(
            fidl::unowned(static_cast<uint8_t*>(const_cast<void*>(op_list[i].data_buffer))));
        reads[i].set_count(op_list[i].data_size);
      }
      fidl::VectorView<fidl::VectorView<uint8_t>> all_reads(fidl::unowned(reads.get()), op_count);
      ctx2->completer->ReplySuccess(std::move(all_reads));
    } else {
      ctx2->completer->ReplyError(status);
    }
    sync_completion_signal(&ctx2->done);
  };
  bus_->Transact(address_, op_list.get(), segments_is_write.count(), callback, &ctx);
  sync_completion_wait(&ctx.done, zx::duration::infinite().get());
}

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
