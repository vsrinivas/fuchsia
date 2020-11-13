// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/general_parameters.h"

namespace analytics::core_dev_tools {

namespace {

constexpr int kOsVersionIndex = 1;

}  // namespace

void GeneralParameters::SetOsVersion(std::string_view os) {
  SetCustomDimension(kOsVersionIndex, os);
}

}  // namespace analytics::core_dev_tools
