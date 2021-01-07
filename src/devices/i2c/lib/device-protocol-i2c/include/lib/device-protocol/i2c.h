// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_H_
#define SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_H_

#include <fuchsia/hardware/i2c/c/banjo.h>
#include <lib/sync/completion.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Writes and reads data on an i2c channel. If both write_length and read_length
// are greater than zero, this call will perform a write operation immediately followed
// by a read operation with no other traffic occurring on the bus in between.
// If read_length is zero, then i2c_write_read will only perform a write operation,
// and if write_length is zero, then it will only perform a read operation.
// The results of the operation are returned asynchronously via the transact_cb.
// The cookie parameter can be used to pass your own private data to the transact_cb callback.
static inline void i2c_write_read(const i2c_protocol_t* i2c, const void* write_buf,
                                  size_t write_length, size_t read_length,
                                  i2c_transact_callback transact_cb, void* cookie) {
  i2c_op_t ops[2];
  size_t count = 0;
  if (write_length) {
    ops[count].data_buffer = (void*)write_buf;
    ops[count].data_size = (uint32_t)write_length;
    ops[count].is_read = false;
    ops[count].stop = !read_length;
    count++;
  }
  if (read_length) {
    ops[count].data_buffer = NULL;
    ops[count].data_size = (uint32_t)read_length;
    ops[count].is_read = true;
    ops[count].stop = true;
    count++;
  }
  i2c_transact(i2c, ops, count, transact_cb, cookie);
}

typedef struct i2c_write_read_ctx {
  sync_completion_t completion;
  void* read_buf;
  size_t read_length;
  zx_status_t result;
#if defined(__cplusplus)
  i2c_write_read_ctx() : read_buf(nullptr), read_length(0), result(ZX_ERR_INTERNAL) {}
#endif
} i2c_write_read_ctx_t;

#if !defined(__cplusplus)
#define I2C_WRITE_READ_CTX_INIT \
  ((i2c_write_read_ctx_t){SYNC_COMPLETION_INIT, NULL, 0, ZX_ERR_INTERNAL})
#endif

static inline void i2c_write_read_sync_cb(void* cookie, zx_status_t status, const i2c_op_t* ops,
                                          size_t cnt) {
  i2c_write_read_ctx_t* ctx = (i2c_write_read_ctx_t*)cookie;
  ctx->result = status;
  if (status == ZX_OK && ctx->read_buf && ctx->read_length) {
    ZX_DEBUG_ASSERT(cnt == 1);
    memcpy(ctx->read_buf, ops[0].data_buffer, ctx->read_length);
  }

  sync_completion_signal(&ctx->completion);
}

static inline zx_status_t i2c_write_read_sync(const i2c_protocol_t* i2c, const void* write_buf,
                                              size_t write_length, void* read_buf,
                                              size_t read_length) {
#if !defined(__cplusplus)
  i2c_write_read_ctx_t ctx = I2C_WRITE_READ_CTX_INIT;
#else
  i2c_write_read_ctx_t ctx;
#endif
  ctx.read_buf = read_buf;
  ctx.read_length = read_length;

  i2c_write_read(i2c, write_buf, write_length, read_length, i2c_write_read_sync_cb, &ctx);
  zx_status_t status = sync_completion_wait(&ctx.completion, ZX_TIME_INFINITE);
  if (status == ZX_OK) {
    return ctx.result;
  } else {
    return status;
  }
}

static inline zx_status_t i2c_write_sync(const i2c_protocol_t* i2c, const void* write_buf,
                                         size_t write_length) {
  return i2c_write_read_sync(i2c, write_buf, write_length, NULL, 0);
}

static inline zx_status_t i2c_read_sync(const i2c_protocol_t* i2c, void* read_buf,
                                        size_t read_length) {
  return i2c_write_read_sync(i2c, NULL, 0, read_buf, read_length);
}

__END_CDECLS

#endif  // SRC_DEVICES_I2C_LIB_DEVICE_PROTOCOL_I2C_INCLUDE_LIB_DEVICE_PROTOCOL_I2C_H_
