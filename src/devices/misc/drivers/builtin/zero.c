// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/types.h>

#include <ddk/device.h>
#include <ddk/driver.h>

static zx_status_t zero_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  memset(buf, 0, count);
  *actual = count;
  return ZX_OK;
}

static zx_status_t zero_write(void* ctx, const void* buf, size_t count, zx_off_t off,
                              size_t* actual) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_protocol_device_t zero_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = zero_read,
    .write = zero_write,
};

zx_status_t zero_bind(void* ctx, zx_device_t* parent, void** cookie) {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "zero",
      .ops = &zero_device_proto,
  };

  zx_device_t* dev;
  return device_add(parent, &args, &dev);
}
