// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdio>
#include <string>

#include "src/lib/fxl/strings/join_strings.h"

int main(int argc, const char** argv) {
  std::vector<std::string> arguments(argv, argv + argc);
  puts(fxl::JoinStrings(arguments, " ").c_str());
  return 0;
}
