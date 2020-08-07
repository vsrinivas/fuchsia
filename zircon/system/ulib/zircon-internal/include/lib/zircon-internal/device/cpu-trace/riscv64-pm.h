// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#pragma once

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zircon-internal/device/cpu-trace/common-pm.h>

namespace perfmon {

// These structs are used for communication between the device driver
// and the kernel.

// Properties of perf data collection on this system.
struct Riscv64PmuProperties {
  PmuCommonProperties common;
};

// Configuration data passed from driver to kernel.
struct Riscv64PmuConfig {
  PmuEventId timebase_event;
};

}  // namespace perfmon

