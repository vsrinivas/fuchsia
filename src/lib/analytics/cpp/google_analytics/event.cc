// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/event.h"

namespace analytics::google_analytics {

namespace {

constexpr char kHitType[] = "event";
constexpr char kCategoryKey[] = "ec";
constexpr char kActionKey[] = "ea";
constexpr char kLabelKey[] = "el";
constexpr char kValueKey[] = "ev";

}  // namespace

Event::Event(std::string_view category, std::string_view action,
             const std::optional<std::string_view>& label, const std::optional<int64_t>& value) {
  SetParameter(Hit::kHitTypeKey, kHitType);
  SetParameter(kCategoryKey, category);
  SetParameter(kActionKey, action);
  if (label.has_value())
    SetParameter(kLabelKey, label.value());
  if (value.has_value())
    SetParameter(kValueKey, std::to_string(value.value()));
}

}  // namespace analytics::google_analytics
