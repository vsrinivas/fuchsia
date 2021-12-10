// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

extern char** environ;

// This is the same as zxtest's default main() except that it checks the kernel
// command line for gtest arguments and passes them through to the test.
// Since this is run directly from boot there's no way for the user to pass
// a "normal" argc/argv.
int main() {
  int argc = 1;
  const char* argv[4] = {"core-tests", NULL, NULL, NULL};

  bool has_filter = false;
  static const char kFilterPrefix[] = "--gtest_filter=";

  bool has_repeat = false;
  static const char kRepeatPrefix[] = "--gtest_repeat=";

  for (char** p = environ; *p; ++p) {
    if (!has_filter && strncmp(*p, kFilterPrefix, sizeof(kFilterPrefix) - 1) == 0) {
      argv[argc++] = *p;
      has_filter = true;
    }

    if (!has_repeat && strncmp(*p, kRepeatPrefix, sizeof(kRepeatPrefix) - 1) == 0) {
      argv[argc++] = *p;
      has_repeat = true;
    }
  }
  return RUN_ALL_TESTS(argc, (char**)argv);
}
