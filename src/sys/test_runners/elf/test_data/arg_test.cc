// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <string.h>

// Expect argc == 2, argv[1] == "expected_arg"
int main(int argc, char** argv) {
  if (argc != 2) {
    printf("Unexpected argc=%d\n", argc);
    return -argc;
  }
  printf("Got argv[1]=\"%s\"\n", argv[1]);
  if (strcmp(argv[1], "expected_arg") != 0) {
    return 1;
  }
  return 0;
}
