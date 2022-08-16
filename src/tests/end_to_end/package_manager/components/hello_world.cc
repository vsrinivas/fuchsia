// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

int main() {
  // TODO(fxb/106529): Remove after bug is fixed.
  for (int i = 0; i < 50; i++) {
    std::cout << "Placeholder statement" << std::endl;
  }
  std::cout << "Hello, World!" << std::endl;
  return 0;
}
