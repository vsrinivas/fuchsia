// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "standalone.h"

// This is the same as zxtest's default main() except that it checks the kernel
// command line for gtest arguments and passes them through to the test.
// Since this is run directly from boot there's no way for the user to pass
// a "normal" argc/argv.
int main() {
  int argc = 1;
  const char* argv[4] = {"core-tests", nullptr, nullptr, nullptr};

  StandaloneOption filter = {"--gtest_filter="};
  StandaloneOption repeat = {"--gtest_repeat="};
  StandaloneGetOptions({filter, repeat});

  if (!filter.option.empty()) {
    argv[argc++] = filter.option.c_str();
  }
  if (!repeat.option.empty()) {
    argv[argc++] = repeat.option.c_str();
  }

  return RUN_ALL_TESTS(argc, const_cast<char**>(argv));
}
