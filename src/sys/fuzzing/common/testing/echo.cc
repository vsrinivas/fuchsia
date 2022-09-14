// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// An extremely simple program that simply reads data from stdin and echoes it to stdout, stderr, or
// both, depending on its command line arguments.
int main(int argc, char** argv) {
  bool echo_out = false;
  bool echo_err = false;
  for (int i = 1; i < argc; ++i) {
    echo_out |= strcmp(argv[i], "--stdout") == 0;
    echo_err |= strcmp(argv[i], "--stderr") == 0;
  }
  int c;
  while ((c = getc(stdin)) != EOF) {
    if (echo_out) {
      putc(c, stdout);
    }
    if (echo_err) {
      putc(c, stderr);
    }
  }
  if (auto* rc = getenv("FUZZING_COMMON_TESTING_ECHO_EXITCODE"); rc != nullptr) {
    return atoi(rc);
  }
  return 0;
}
