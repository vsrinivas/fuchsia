// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include "src/graphics/display/drivers/intel-i915-tgl/hardware-common.h"
#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"

namespace i915_tgl {

cpp20::span<const DdiId> GetDdiIds(uint16_t device_id) {
  if (is_skl(device_id)) {
    return DdiIds<tgl_registers::Platform::kSkylake>();
  }
  if (is_kbl(device_id)) {
    return DdiIds<tgl_registers::Platform::kKabyLake>();
  }
  if (is_tgl(device_id)) {
    return DdiIds<tgl_registers::Platform::kTigerLake>();
  }
  if (is_test_device(device_id)) {
    return DdiIds<tgl_registers::Platform::kTestDevice>();
  }
  ZX_ASSERT_MSG(false, "Device id (%04x) not supported", device_id);
}

}  // namespace i915_tgl
