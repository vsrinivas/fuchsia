// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/binding.h>
#include <lib/ddk/debug.h>
#include <lib/ddk/device.h>
#include <lib/ddk/platform-defs.h>
#include <stdlib.h>
#include <string.h>

#include <array>

typedef struct cpu_trace_dev {
  zx_device_t* zxdev;
  zx_handle_t bti;
} cpu_trace_dev_t;

static const pdev_device_info_t cpu_trace_pdev_device_info = []() {
  pdev_device_info_t info{};
  info.vid = PDEV_VID_GENERIC;
  info.pid = PDEV_PID_GENERIC;
  info.did = PDEV_DID_CPU_TRACE;
  info.bti_count = 1;
  return info;
}();

static zx_status_t cpu_trace_get_bti(void* ctx, uint32_t index, zx_handle_t* out_handle) {
  auto dev = reinterpret_cast<cpu_trace_dev_t*>(ctx);
  if (index >= cpu_trace_pdev_device_info.bti_count || out_handle == NULL) {
    return ZX_ERR_INVALID_ARGS;
  }
  return zx_handle_duplicate(dev->bti, ZX_RIGHT_SAME_RIGHTS, out_handle);
}

static zx_status_t cpu_trace_get_device_info(void* ctx, pdev_device_info_t* out_info) {
  memcpy(out_info, &cpu_trace_pdev_device_info, sizeof(*out_info));
  return ZX_OK;
}

static zx_status_t cpu_trace_get_mmio(void* ctx, uint32_t index, pdev_mmio_t* mmio) {
  return ZX_ERR_NOT_SUPPORTED;
}

static zx_status_t cpu_trace_get_interrupt(void* ctx, uint32_t index, uint32_t flags,
                                           zx_handle_t* out_handle) {
  return ZX_ERR_NOT_SUPPORTED;
}

static pdev_protocol_ops_t cpu_trace_proto_ops = []() {
  pdev_protocol_ops_t ops{};
  ops.get_mmio = cpu_trace_get_mmio;
  ops.get_interrupt = cpu_trace_get_interrupt;
  ops.get_bti = cpu_trace_get_bti;
  ops.get_device_info = cpu_trace_get_device_info;
  return ops;
}();

static void cpu_trace_release(void* ctx) {
  auto dev = reinterpret_cast<cpu_trace_dev_t*>(ctx);
  zx_handle_close(dev->bti);
  free(dev);
}

static zx_protocol_device_t cpu_trace_dev_proto = []() {
  zx_protocol_device_t device{};
  device.version = DEVICE_OPS_VERSION;
  device.release = cpu_trace_release;
  return device;
}();

zx_status_t publish_cpu_trace(zx_handle_t bti, zx_device_t* sys_root) {
  cpu_trace_dev_t* dev = reinterpret_cast<cpu_trace_dev_t*>(calloc(1, sizeof(*dev)));
  if (dev == NULL) {
    return ZX_ERR_NO_MEMORY;
  }
  dev->bti = bti;

  zx_device_prop_t props[] = {
      {BIND_PLATFORM_DEV_VID, 0, cpu_trace_pdev_device_info.vid},
      {BIND_PLATFORM_DEV_PID, 0, cpu_trace_pdev_device_info.pid},
      {BIND_PLATFORM_DEV_DID, 0, cpu_trace_pdev_device_info.did},
  };
  device_add_args_t args{};
  args.version = DEVICE_ADD_ARGS_VERSION;
  args.name = "cpu-trace";
  args.ctx = dev;
  args.ops = &cpu_trace_dev_proto;
  args.props = props;
  args.prop_count = std::size(props);
  args.proto_id = ZX_PROTOCOL_PDEV;
  args.proto_ops = &cpu_trace_proto_ops;
  args.proxy_args = NULL;
  args.flags = 0;

  // add as a child of the sysroot
  zx_status_t status = device_add(sys_root, &args, &dev->zxdev);
  if (status != ZX_OK) {
    zxlogf(ERROR, "platform-bus: error %d in device_add(sys/cpu-trace)", status);
    cpu_trace_release(dev);
    return status;
  }

  return ZX_OK;
}
