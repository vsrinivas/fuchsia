// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_USER_AGENT_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_USER_AGENT_H_

#include <string>
#include <string_view>

namespace analytics::core_dev_tools::internal {

// Generate a user agent string such that Google Analytics could correctly identify the operating
// system of a hit.
std::string GenerateUserAgent(std::string_view tool_name);

}  // namespace analytics::core_dev_tools::internal

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_USER_AGENT_H_
