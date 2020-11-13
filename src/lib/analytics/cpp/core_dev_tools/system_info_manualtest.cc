// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/lib/analytics/cpp/core_dev_tools/system_info.h"

using analytics::GetOsVersion;

int main() {
  std::cout << GetOsVersion() << std::endl;
  return 0;
}
