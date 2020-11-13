// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/lib/analytics/cpp/core_dev_tools/toolchain.h"

using analytics::GetToolchainInfo;
using analytics::ToolchainInfo;

int main() {
  ToolchainInfo toolchain_info = GetToolchainInfo();
  std::cout << "Toolchain: " << ToString(toolchain_info.toolchain) << std::endl;
  std::cout << "Version: " << toolchain_info.version << std::endl;

  return 0;
}
