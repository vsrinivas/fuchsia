// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/hid-input-report/axis.h"

namespace hid_input_report {

void SetAxisFromAttribute(const hid::Attributes& attrs, Axis* axis) {
  axis->enabled = true;
  axis->range.min = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.min)));
  axis->range.max = static_cast<int64_t>(
      hid::unit::ConvertValToUnitType(attrs.unit, static_cast<double>(attrs.phys_mm.max)));
  axis->unit = hid::unit::GetUnitTypeFromUnit(attrs.unit);
}

}  // namespace hid_input_report
