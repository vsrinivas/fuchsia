// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/color_conversion_state_machine.h"

namespace flatland {

const std::array<float, 9> kDefaultColorConversionCoefficients = {1, 0, 0, 0, 1, 0, 0, 0, 1};
const std::array<float, 3> kDefaultColorConversionOffsets = {0, 0, 0};

void ColorConversionStateMachine::SetApplyConfigSucceeded() {
  // Save the current data as applied data and mark |dc_has_cc_| as true only if
  // the data is not default, since applying default CC data and not applying any
  // at all are the same thing.
  applied_data_ = data_;
  dc_has_cc_ = (applied_data_ != ColorConversionData());
}

void ColorConversionStateMachine::SetData(const ColorConversionData& data) { data_ = data; }

std::optional<ColorConversionData> ColorConversionStateMachine::GetDataToApply() {
  // Return the actual data if it differs from the applied data. This works even
  // if there is no applied data, since in that case the applied_data is default and we
  // would then return nullopt if the current data is also default.
  return (data_ == applied_data_) ? std::nullopt : std::optional<ColorConversionData>(data_);
}

bool ColorConversionStateMachine::GpuRequiresDisplayClearing() {
  // Return true if the DC has CC state applied but the current data differs.
  return (dc_has_cc_) && (data_ != applied_data_);
}

void ColorConversionStateMachine::DisplayCleared() {
  dc_has_cc_ = false;
  applied_data_ = ColorConversionData();
}

}  // namespace flatland
