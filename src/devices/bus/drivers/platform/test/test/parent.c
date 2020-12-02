// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

#include "src/devices/bus/drivers/platform/test/test-parent-bind.h"

#define DRIVER_NAME "test-parent"

typedef struct {
  zx_device_t* zxdev;
} test_t;

static void test_release(void* ctx) { free(ctx); }

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .release = test_release,
};

static zx_status_t test_bind(void* ctx, zx_device_t* parent) {
  pdev_protocol_t pdev;
  zx_status_t status;

  zxlogf(INFO, "test_bind: %s ", DRIVER_NAME);

  status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: could not get ZX_PROTOCOL_PDEV", DRIVER_NAME);
    return status;
  }

  test_t* test = calloc(1, sizeof(test_t));
  if (!test) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_device_prop_t child_props[] = {
      {BIND_PLATFORM_DEV_VID, 0, PDEV_VID_TEST},
      {BIND_PLATFORM_DEV_PID, 0, PDEV_PID_PBUS_TEST},
      {BIND_PLATFORM_DEV_DID, 0, PDEV_DID_TEST_CHILD_1},
  };

  device_add_args_t child_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "child-1",
      .ctx = test,
      .ops = &test_device_protocol,
      .props = child_props,
      .prop_count = countof(child_props),
  };

  status = device_add(parent, &child_args, &test->zxdev);
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

ZIRCON_DRIVER(test_bus, test_driver_ops, "zircon", "0.1");
