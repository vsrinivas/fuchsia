// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

extern char **environ;

// This is the same as zxtest's default main() except that it allows filtering
// based on an argument passed in from the kernel command line, as there's no
// way for the user to pass a "normal" argc/argv.
int main() {
  int argc = 1;
  const char* argv[3] = {"core-tests", NULL, NULL};
  for (char** p = environ; *p; ++p) {
    static const char kPrefix[] = "--gtest_filter=";
    if (strncmp(*p, kPrefix, sizeof(kPrefix) - 1) == 0) {
      argv[argc++] = *p;
      break;
    }
  }
  return RUN_ALL_TESTS(argc, (char**)argv);
}
