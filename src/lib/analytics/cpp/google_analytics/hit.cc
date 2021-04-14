// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/hit.h"

namespace analytics::google_analytics {

Hit::~Hit() = default;

void Hit::AddGeneralParameters(const GeneralParameters& general_parameters) {
  const auto& parameters = general_parameters.parameters();
  parameters_.insert(parameters.begin(), parameters.end());
}

void Hit::SetParameter(std::string name, std::string_view value) {
  parameters_[std::move(name)] = value;
}

}  // namespace analytics::google_analytics
