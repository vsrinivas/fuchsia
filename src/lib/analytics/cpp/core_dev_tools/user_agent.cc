// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/user_agent.h"

#include "src/lib/fxl/strings/substitute.h"

namespace analytics::core_dev_tools::internal {

namespace {

#if defined(__linux__)
constexpr char kOs[] = "Linux";
#elif defined(__APPLE__)
constexpr char kOs[] = "Macintosh";
#else
constexpr char kOs[] = "";
#endif

}  // namespace

std::string GenerateUserAgent(std::string_view tool_name) {
  return kOs[0] == '\0' ? fxl::Substitute("Fuchsia $0", tool_name)
                        : fxl::Substitute("Fuchsia $0($1)", tool_name, kOs);
}

}  // namespace analytics::core_dev_tools::internal
