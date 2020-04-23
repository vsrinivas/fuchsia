// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/device.h>

#define DRIVER_NAME "test-child-4"

typedef struct {
  zx_device_t* zxdev;
  zx_handle_t rpc_channel;
} test_t;

static void test_release(void* ctx) {
  test_t* dev = (test_t*)ctx;
  zx_handle_close(dev->rpc_channel);
  free(dev);
}

static zx_status_t test_get_protocol(void* ctx, uint32_t protocol_id, void* proto) {
  // Lie about supporting the CLOCK protocol.  The composite device will just
  // check that we claimed to support it.  Note the non-proxied device does
  // not claim to support this protocol, so if we see it, we must be talking
  // to the proxy.
  if (protocol_id == ZX_PROTOCOL_CLOCK) {
    // Zero out the proto ops in case something tries using it
    memset(proto, 0, sizeof(uintptr_t));
    return ZX_OK;
  }
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t test_device_protocol = {
    .version = DEVICE_OPS_VERSION,
    .get_protocol = test_get_protocol,
    .release = test_release,
};
static zx_status_t test_create(void* ctx, zx_device_t* parent, const char* name, const char* args,
                               zx_handle_t rpc_channel) {
  zx_status_t status;

  zxlogf(INFO, "test_create: %s ", DRIVER_NAME);

  test_t* test = calloc(1, sizeof(test_t));
  if (!test) {
    return ZX_ERR_NO_MEMORY;
  }
  test->rpc_channel = rpc_channel;

  device_add_args_t dev_args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "child-4",
      .ctx = test,
      .ops = &test_device_protocol,
  };

  status = device_add(parent, &dev_args, &test->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "%s: device_add failed: %d", DRIVER_NAME, status);
    free(test);
    return status;
  }

  return ZX_OK;
}

static zx_driver_ops_t test_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .create = test_create,
};

ZIRCON_DRIVER_BEGIN(test_bus, test_driver_ops, "zircon", "0.1", 1)
BI_ABORT_IF_AUTOBIND
ZIRCON_DRIVER_END(test_bus)
