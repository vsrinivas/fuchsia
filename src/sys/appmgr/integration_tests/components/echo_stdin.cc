// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <cstdio>
#include <string>

#include "src/lib/fxl/strings/join_strings.h"

int main(int argc, const char** argv) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  std::vector<std::string> arguments(argv, argv + argc);
  auto print = fxl::JoinStrings(arguments, " ");
  if (write(STDIN_FILENO, print.c_str(), print.length()) >= 0) {
    // We should NOT be able to write to stdin.
    return -1;
  }
  char buf;
  if (read(STDIN_FILENO, &buf, sizeof(buf)) != 0) {
    // stdin should be closed.
    return -1;
  }
  return 0;
}
