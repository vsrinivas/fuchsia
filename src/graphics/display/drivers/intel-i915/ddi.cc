// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/display/drivers/intel-i915/ddi.h"

#include <lib/stdcompat/span.h>
#include <zircon/assert.h>

#include "src/graphics/display/drivers/intel-i915/pci-ids.h"
#include "src/graphics/display/drivers/intel-i915/registers-ddi.h"

namespace i915 {

namespace {

const registers::Ddi kSklDdis[] = {
    registers::DDI_A, registers::DDI_B, registers::DDI_C, registers::DDI_D, registers::DDI_E,
};

}  // namespace

cpp20::span<const registers::Ddi> GetDdis(uint16_t device_id) {
  if (is_skl(device_id) || is_kbl(device_id)) {
    return {kSklDdis, sizeof(kSklDdis) / sizeof(registers::Ddi)};
  }
  if (is_test_device(device_id)) {
    return {kSklDdis, sizeof(kSklDdis) / sizeof(registers::Ddi)};
  }
  ZX_ASSERT_MSG(false, "Device id (%04x) not supported", device_id);
}

}  // namespace i915
