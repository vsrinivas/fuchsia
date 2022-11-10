// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "src/lib/fxl/command_line.h"
#include "src/lib/files/file.h"
#include "src/lib/files/glob.h"

int main() {
  std::cout << "Hello, my dear in-tree Bazel world!\n";
  fxl::CommandLine();
  files::IsFile("/tmp/test");
  files::Glob("mypath");
  return 0;
}
