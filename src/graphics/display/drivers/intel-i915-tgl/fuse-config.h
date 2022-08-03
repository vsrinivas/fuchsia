// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FUSE_CONFIG_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FUSE_CONFIG_H_

#include <lib/mmio/mmio.h>

namespace i915_tgl {

// Device configuration that is immutable for the driver's lifetime.
//
// This covers configuration data that meets the following constraints:
// 1) Does not change for the driver's lifetime.
// 2) Is outside the driver's control. Our responsibility is to read the data
//    and tailor the driver's behavior accordingly.
// 3) Is stored in the device hardware, usually in fuses and straps.
//    Configuration stored in other spaces, such as the Video BIOS Table, is
//    outside this class' responsibility.
struct FuseConfig {
  static FuseConfig ReadFrom(fdf::MmioBuffer& mmio_space, int device_id);

  // Logs non-default configuration, such as disabled hardware units.
  void Log();

  // Default values are chosen to minimize the impact of forgetting to
  // explicitly initialize fields. For example, "enabled" fields default to
  // false, so the driver doesn't attempt to use hardware that may not work.

  int core_clock_limit_khz = 0;

  bool graphics_enabled = false;
  bool pipe_enabled[4] = {false, false, false, false};
  bool edp_enabled = false;
  bool display_capture_enabled = false;
  bool display_stream_compression_enabled = false;
  bool frame_buffer_compression_enabled = false;
  bool display_power_savings_enabled = false;
};

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_FUSE_CONFIG_H_
