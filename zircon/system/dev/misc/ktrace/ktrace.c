// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/tracing/kernel/c/fidl.h>
#include <lib/zircon-internal/ktrace.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>

static zx_status_t ktrace_read(void* ctx, void* buf, size_t count, zx_off_t off, size_t* actual) {
  size_t length;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_ktrace_read(get_root_resource(), buf, off, count, &length);
  if (status == ZX_OK) {
    *actual = length;
  }
  return status;
}

static zx_off_t ktrace_get_size(void* ctx) {
  size_t size;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_ktrace_read(get_root_resource(), NULL, 0, 0, &size);
  return status != ZX_OK ? (zx_off_t)status : (zx_off_t)size;
}

static zx_status_t fidl_Start(void* ctx, uint32_t group_mask, fidl_txn_t* txn) {
  zx_status_t status =
      // Please do not use get_root_resource() in new code. See ZX-1467.
      zx_ktrace_control(get_root_resource(), KTRACE_ACTION_START, group_mask, NULL);
  return fuchsia_tracing_kernel_ControllerStart_reply(txn, status);
}

static zx_status_t fidl_Stop(void* ctx, fidl_txn_t* txn) {
  zx_status_t status =
      // Please do not use get_root_resource() in new code. See ZX-1467.
      zx_ktrace_control(get_root_resource(), KTRACE_ACTION_STOP, 0, NULL);
  return fuchsia_tracing_kernel_ControllerStop_reply(txn, status);
}

static zx_status_t fidl_Rewind(void* ctx, fidl_txn_t* txn) {
  zx_status_t status =
      // Please do not use get_root_resource() in new code. See ZX-1467.
      zx_ktrace_control(get_root_resource(), KTRACE_ACTION_REWIND, 0, NULL);
  return fuchsia_tracing_kernel_ControllerRewind_reply(txn, status);
}

static zx_status_t fidl_GetBytesWritten(void* ctx, fidl_txn_t* txn) {
  size_t size = 0;
  // Please do not use get_root_resource() in new code. See ZX-1467.
  zx_status_t status = zx_ktrace_read(get_root_resource(), NULL, 0, 0, &size);
  return fuchsia_tracing_kernel_ControllerGetBytesWritten_reply(txn, status, size);
}

static const fuchsia_tracing_kernel_Controller_ops_t fidl_ops = {
    .Start = fidl_Start,
    .Stop = fidl_Stop,
    .Rewind = fidl_Rewind,
    .GetBytesWritten = fidl_GetBytesWritten,
};

static zx_status_t ktrace_message(void* ctx, fidl_msg_t* msg, fidl_txn_t* txn) {
  return fuchsia_tracing_kernel_Controller_dispatch(ctx, txn, msg, &fidl_ops);
}

static zx_protocol_device_t ktrace_device_proto = {
    .version = DEVICE_OPS_VERSION,
    .read = ktrace_read,
    .get_size = ktrace_get_size,
    .message = ktrace_message,
};

static zx_status_t ktrace_bind(void* ctx, zx_device_t* parent) {
  device_add_args_t args = {
      .version = DEVICE_ADD_ARGS_VERSION,
      .name = "ktrace",
      .ops = &ktrace_device_proto,
  };

  zx_device_t* dev;
  return device_add(parent, &args, &dev);
}

static zx_driver_ops_t ktrace_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ktrace_bind,
};

ZIRCON_DRIVER_BEGIN(ktrace, ktrace_driver_ops, "zircon", "0.1", 1)
BI_MATCH_IF(EQ, BIND_PROTOCOL, ZX_PROTOCOL_MISC_PARENT), ZIRCON_DRIVER_END(ktrace)
