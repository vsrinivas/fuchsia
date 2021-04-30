// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/google_analytics/general_parameters.h"

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace analytics::google_analytics {

namespace {

// Google Analytics custom dimensions.
// See https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#cd_
constexpr char kCustomDimensionKeyFormat[] = "cd%d";
constexpr int kCustomDimensionIndexMin = 1;
constexpr int kCustomDimensionIndexMax = 200;

// Google Analytics custom metrics.
// See https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters#cm_
constexpr char kCustomMetricKeyFormat[] = "cm%d";
constexpr int kCustomMetricIndexMin = 1;
constexpr int kCustomMetricIndexMax = 200;

// Other general parameters.
// See https://developers.google.com/analytics/devguides/collection/protocol/v1/parameters
constexpr char kApplicationNameKey[] = "an";
constexpr char kApplicationVersionKey[] = "av";
constexpr char kDataSourceKey[] = "ds";

}  // namespace

GeneralParameters::~GeneralParameters() = default;

void GeneralParameters::SetCustomDimension(int index, std::string_view value) {
  FX_DCHECK(index >= kCustomDimensionIndexMin && index <= kCustomDimensionIndexMax);
  parameters_[fxl::StringPrintf(kCustomDimensionKeyFormat, index)] = value;
}

void GeneralParameters::SetCustomMetric(int index, int64_t value) {
  FX_DCHECK(index >= kCustomMetricIndexMin && index <= kCustomMetricIndexMax);
  parameters_[fxl::StringPrintf(kCustomMetricKeyFormat, index)] = std::to_string(value);
}

void GeneralParameters::SetApplicationName(std::string_view application_name) {
  parameters_[kApplicationNameKey] = application_name;
}

void GeneralParameters::SetApplicationVersion(std::string_view application_version) {
  parameters_[kApplicationVersionKey] = application_version;
}

void GeneralParameters::SetDataSource(std::string_view data_source) {
  parameters_[kDataSourceKey] = data_source;
}

}  // namespace analytics::google_analytics
