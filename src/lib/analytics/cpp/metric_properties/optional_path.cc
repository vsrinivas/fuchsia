// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/metric_properties/optional_path.h"

namespace analytics::metric_properties::internal {

std::optional<std::filesystem::path> GetOptionalPathFromEnv(const char* env) {
  const char* dir = std::getenv(env);
  if (dir) {
    return dir;
  }
  return std::nullopt;
}

}  // namespace analytics::metric_properties::internal
