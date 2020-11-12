// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>

#include "src/devices/bus/drivers/platform/test/test-child-3-bind.h"

#define DRIVER_NAME "test-child-3"

typedef struct {
  zx_device_t* zxdev;
} test_t;

static void test_release(void* ctx) { free(ctx); }

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = test_release,
};

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
  zx_status_t status;

  zxlogf(INFO, "test_bind: %s ", DRIVER_NAME);

  test_t* test = calloc(1, sizeof(test_t));
  if (!test) {
    return ZX_ERR_NO_MEMORY;
  }

  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "child-3",
      .ctx = test,
      .ops = &test_device_protocol,
  };

  status = device_add(parent, &args, &test->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add failed: %d", DRIVER_NAME, status);
    free(test);
    return status;
  }

  return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = test_bind,
};

ZIRCON_DRIVER(test_child_3, test_driver_ops, "zircon", "0.1")
