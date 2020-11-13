// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_TOOLCHAIN_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_TOOLCHAIN_H_

#include <string>

namespace analytics {

enum class Toolchain { kInTree, kSdk, kOther };

struct ToolchainInfo {
  Toolchain toolchain;
  std::string version;
};

// Get the toolchain which the executable belongs to, and the version of the toolchain
ToolchainInfo GetToolchainInfo();

// Convert Toolchain enum to string
std::string ToString(Toolchain toolchain);

}  // namespace analytics

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_TOOLCHAIN_H_
