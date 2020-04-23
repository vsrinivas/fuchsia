// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <lib/device-protocol/i2c.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>
#include <zircon/process.h>
#include <zircon/syscalls.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/i2c.h>
#include <ddk/protocol/platform/device.h>

typedef struct {
  zx_device_t* zxdev;
  i2c_protocol_t i2c;
  thrd_t thread;
  bool done;
} i2c_test_t;

static void i2c_test_release(void* ctx) {
  i2c_test_t* bus = ctx;

  bus->done = true;
  thrd_join(bus->thread, NULL);
  free(bus);
}

static zx_protocol_device_t i2c_test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = i2c_test_release,
};

static void i2c_complete(void* cookie, zx_status_t status, const i2c_op_t* ops, size_t cnt) {
  if (status != ZX_OK) {
    zxlogf(ERROR, "gauss-i2c-test i2c_complete error: %d", status);
  }
  ZX_ASSERT(cnt == 1);
  if (ops[0].data_size != 8) {
    zxlogf(ERROR, "gauss-i2c-test received %zd bytes instead of 8", ops[0].data_size);
  }
  uint8_t* data = (uint8_t*)ops[0].data_buffer;
  zxlogf(INFO, "gauss-i2c-test: %02X %02X %02X %02X %02X %02X %02X %02X", data[0], data[1],
         data[2], data[3], data[4], data[5], data[6], data[7]);
}

static int i2c_test_thread(void* arg) {
  i2c_test_t* i2c_test = arg;

  while (!i2c_test->done) {
    char write_buf[1] = {0};
    i2c_write_read(&i2c_test->i2c, write_buf, sizeof(write_buf), 8, i2c_complete, NULL);
    sleep(1);
  }

  return 0;
}

static zx_status_t i2c_test_bind(void* ctx, zx_device_t* parent) {
  i2c_test_t* i2c_test = calloc(1, sizeof(i2c_test_t));
  if (!i2c_test) {
    return ZX_ERR_NO_MEMORY;
  }

  if (device_get_protocol(parent, ZX_PROTOCOL_I2C, &i2c_test->i2c) != ZX_OK) {
    free(i2c_test);
    return ZX_ERR_NOT_SUPPORTED;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "gauss-i2c-test",
      .ctx = i2c_test,
      .ops = &i2c_test_device_protocol,
      .flags = DEVICE_ADD_NON_BINDABLE,
  };

  zx_status_t status = device_add(parent, &args, NULL);
  if (status != ZX_OK) {
    free(i2c_test);
    return status;
  }

  thrd_create_with_name(&i2c_test->thread, i2c_test_thread, i2c_test, "i2c_test_thread");
  return ZX_OK;
}

static zx_driver_ops_t i2c_test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = i2c_test_bind,
};

ZIRCON_DRIVER_BEGIN(gauss_i2c_test, i2c_test_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GOOGLE),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GAUSS),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_GAUSS_I2C_TEST),
    ZIRCON_DRIVER_END(gauss_i2c_test)
