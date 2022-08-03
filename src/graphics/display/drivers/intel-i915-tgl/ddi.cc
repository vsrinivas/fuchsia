// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915-tgl/ddi.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include "src/graphics/display/drivers/intel-i915-tgl/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915-tgl/registers-ddi.h"

namespace i915_tgl {

const std::array<tgl_registers::Ddi, 5> kSklDdis = {
    tgl_registers::DDI_A, tgl_registers::DDI_B, tgl_registers::DDI_C,
    tgl_registers::DDI_D, tgl_registers::DDI_E,
};

const std::array<tgl_registers::Ddi, 9> kTglDdis = {
    tgl_registers::DDI_A,    tgl_registers::DDI_B,    tgl_registers::DDI_C,
    tgl_registers::DDI_TC_1, tgl_registers::DDI_TC_2, tgl_registers::DDI_TC_3,
    tgl_registers::DDI_TC_4, tgl_registers::DDI_TC_5, tgl_registers::DDI_TC_6,
};

cpp20::span<const tgl_registers::Ddi> GetDdis(uint16_t device_id) {
  if (is_skl(device_id) || is_kbl(device_id)) {
    return kSklDdis;
  }
  if (is_tgl(device_id)) {
    return kTglDdis;
  }
  if (is_test_device(device_id)) {
    return kSklDdis;
  }
  ZX_ASSERT_MSG(false, "Device id (%04x) not supported", device_id);
}

}  // namespace i915_tgl
