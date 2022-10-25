// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <string>

std::string GetUsage() {
  return R"(WARNING: this tool is deprecated (fxb/102727) and will be deleted.
Please use `ffx session` commands instead:

    * `sessionctl restart_session` -> `ffx session restart`
    * `sessionctl add_mod` -> `ffx session add`
)";
}

int main(int argc, const char** argv) {
  std::cout << GetUsage() << std::endl;
  return 1;
}
