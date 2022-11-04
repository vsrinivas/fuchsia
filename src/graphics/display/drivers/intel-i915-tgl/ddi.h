// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_H_
#define SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_H_

#include <lib/stdcompat/span.h>

#include <array>

#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

// Get the list of DDIs supported by the device of |device_id|.
cpp20::span<const DdiId> GetDdiIds(uint16_t device_id);

}  // namespace i915_tgl

#endif  // SRC_GRAPHICS_DISPLAY_DRIVERS_INTEL_I915_TGL_DDI_H_
