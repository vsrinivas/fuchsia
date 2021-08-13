// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/exception.h"

namespace analytics::google_analytics {

namespace {

constexpr char kHitType[] = "exception";
constexpr char kDescriptionKey[] = "exd";
constexpr char kFatalKey[] = "exf";

}  // namespace

Exception::Exception(std::optional<std::string_view> description, std::optional<bool> is_fatal) {
  SetParameter(Hit::kHitTypeKey, kHitType);
  if (description.has_value())
    SetParameter(kDescriptionKey, description.value());
  if (is_fatal.has_value())
    SetParameter(kFatalKey, is_fatal.value() ? "1" : "0");
}

}  // namespace analytics::google_analytics
