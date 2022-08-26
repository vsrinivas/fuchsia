// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// See the README.md in this directory for documentation.

#include <fuchsia/hardware/platform/device/c/banjo.h>
#include <lib/ddk/device.h>
#include <lib/ddk/driver.h>
#include <lib/ddk/platform-defs.h>
#include <stdlib.h>
#include <string.h>
#include <zircon/syscalls.h>

#include "cpu-trace-private.h"
#include "src/devices/misc/drivers/cpu-trace/cpu_trace_bind.h"

static constexpr zx_driver_ops_t cpu_trace_driver_ops = []() {
  zx_driver_ops_t ops{};
  ops.version = DRIVER_OPS_VERSION;
  ops.bind = perfmon_bind;
  return ops;
}();

ZIRCON_DRIVER(cpu_trace, cpu_trace_driver_ops, "zircon", "0.1");
