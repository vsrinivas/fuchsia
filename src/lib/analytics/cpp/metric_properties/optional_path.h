// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_OPTIONAL_PATH_H_
#define SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_OPTIONAL_PATH_H_

#include <filesystem>
#include <optional>

namespace analytics::metric_properties::internal {

std::optional<std::filesystem::path> GetOptionalPathFromEnv(const char* env);

}  // namespace analytics::metric_properties::internal

#endif  // SRC_LIB_ANALYTICS_CPP_METRIC_PROPERTIES_OPTIONAL_PATH_H_
