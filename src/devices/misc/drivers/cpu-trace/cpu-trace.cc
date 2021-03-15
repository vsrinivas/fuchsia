// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/platform-defs.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include <ddk/device.h>
#include <ddk/driver.h>

#include "cpu-trace-private.h"
#include "src/devices/misc/drivers/cpu-trace/cpu_trace_bind.h"

static zx_status_t cpu_trace_bind(void* ctx, zx_device_t* parent) {
#ifdef __x86_64__
  zx_status_t perfmon_status = perfmon_bind(ctx, parent);
  zx_status_t insntrace_status = insntrace_bind(ctx, parent);

  // If at least one succeeded return ZX_OK.
  if (perfmon_status != ZX_OK && insntrace_status != ZX_OK) {
    // Doesn't matter which one we return.
    return perfmon_status;
  }
#endif

#ifdef __aarch64__
  zx_status_t perfmon_status = perfmon_bind(ctx, parent);
  if (perfmon_status != ZX_OK) {
    return perfmon_status;
  }
#endif

  return ZX_OK;
}

static constexpr zx_driver_ops_t cpu_trace_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = cpu_trace_bind;
  return ops;
}();

ZIRCON_DRIVER(cpu_trace, cpu_trace_driver_ops, "zircon", "0.1");
