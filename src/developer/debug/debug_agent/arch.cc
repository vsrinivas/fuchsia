// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/arch.h"

#include <zircon/features.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include "src/developer/debug/shared/logging/logging.h"

namespace debug_agent {
namespace arch {

uint32_t GetHardwareBreakpointCount() {
  constexpr uint32_t kUninitialized = static_cast<uint32_t>(-1);
  static uint32_t hw_breakpoint_count = kUninitialized;

  if (hw_breakpoint_count == kUninitialized) {
    if (zx_status_t status =
            zx_system_get_features(ZX_FEATURE_KIND_HW_BREAKPOINT_COUNT, &hw_breakpoint_count);
        status == ZX_OK) {
      DEBUG_LOG(Agent) << "Got HW breakpoint count: " << hw_breakpoint_count;
    } else {
      FX_LOGS(WARNING) << "Could not get HW breakpoint count: " << zx_status_get_string(status);
      hw_breakpoint_count = 0;
    }
  }
  return hw_breakpoint_count;
}

uint32_t GetHardwareWatchpointCount() {
  constexpr uint32_t kUninitialized = static_cast<uint32_t>(-1);
  static uint32_t hw_watchpoint_count = kUninitialized;

  if (hw_watchpoint_count == kUninitialized) {
    if (zx_status_t status =
            zx_system_get_features(ZX_FEATURE_KIND_HW_WATCHPOINT_COUNT, &hw_watchpoint_count);
        status == ZX_OK) {
      DEBUG_LOG(Agent) << "Got HW watchpoint count: " << hw_watchpoint_count;
    } else {
      FX_LOGS(WARNING) << "Could not get HW watchpoint count: " << zx_status_get_string(status);
      hw_watchpoint_count = 0;
    }
  }
  return hw_watchpoint_count;
}

}  // namespace arch
}  // namespace debug_agent
