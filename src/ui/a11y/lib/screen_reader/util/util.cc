// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

#include <lib/syslog/cpp/macros.h>

#include <string>

namespace a11y {

bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node) {
  if (!node) {
    return false;
  }
  if (node->has_states() && node->states().has_hidden() && node->states().hidden()) {
    return false;
  }

  bool contains_text = node->has_attributes() && node->attributes().has_label() &&
                       !node->attributes().label().empty();
  bool is_actionable =
      node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::BUTTON;
  return contains_text || is_actionable;
}

std::string FormatFloat(float input) {
  auto output = std::to_string(input);
  FX_DCHECK(!output.empty());

  // If the output string does not contain a decimal point, we don't need to
  // trim trailing zeros.
  auto location_of_decimal = output.find('.');
  if (location_of_decimal == std::string::npos) {
    return output;
  }

  auto location_of_last_non_zero_character = output.find_last_not_of('0');
  // If the last non-zero character is a decimal point, the value is an integer,
  // so we should remove the decimal point and trailing zeros.
  if (location_of_last_non_zero_character == location_of_decimal) {
    return output.erase(location_of_decimal, std::string::npos);
  }

  // If the last digit is non-zero, the string has no trailing zeros, so return
  // the string as is.
  if (location_of_last_non_zero_character == output.size() - 1) {
    return output;
  }

  // In the last remainig case, the string represents a decimal with trailing
  // zeros.
  return output.erase(location_of_last_non_zero_character + 1, std::string::npos);
}

}  // namespace a11y
