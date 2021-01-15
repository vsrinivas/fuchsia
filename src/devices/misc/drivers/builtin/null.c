// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>

// null is the /dev/null device.

static zx_status_t null_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  *actual = 0;
  return ZX_OK;
}

static zx_status_t null_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                              size_t* actual) {
  *actual = count;
  return ZX_OK;
}

static zx_protocol_device_t null_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = null_read,
    .write = null_write,
};

zx_status_t null_bind(void* ctx, zx_device_t* parent, void** cookie) {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "null",
      .ops = &null_device_proto,
  };

  zx_device_t* dev;
  return device_add(parent, &args, &dev);
}
