// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

namespace {

const tgl_registers::Ddi kSklDdis[] = {
    tgl_registers::DDI_A, tgl_registers::DDI_B, tgl_registers::DDI_C,
    tgl_registers::DDI_D, tgl_registers::DDI_E,
};

}  // namespace

cpp20::span<const tgl_registers::Ddi> GetDdis(uint16_t device_id) {
  if (is_skl(device_id) || is_kbl(device_id)) {
    return {kSklDdis, sizeof(kSklDdis) / sizeof(tgl_registers::Ddi)};
  }
  if (is_test_device(device_id)) {
    return {kSklDdis, sizeof(kSklDdis) / sizeof(tgl_registers::Ddi)};
  }
  ZX_ASSERT_MSG(false, "Device id (%04x) not supported", device_id);
}

}  // namespace i915_tgl
