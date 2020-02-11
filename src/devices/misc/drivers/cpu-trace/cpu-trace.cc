// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <stdlib.h>
#include <string.h>

#include <ddk/binding.h>
#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>

#include <zircon/syscalls.h>

#include "cpu-trace-private.h"

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

ZIRCON_DRIVER_BEGIN(cpu_trace, cpu_trace_driver_ops, "zircon", "0.1", 4)
BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_CPU_TRACE), ZIRCON_DRIVER_END(cpu_trace)
