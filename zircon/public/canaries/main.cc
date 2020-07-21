// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>

extern int foo(int);

int main(int argc, const char** argv) {
  if (argc < 2)
    return EXIT_FAILURE;

  printf("x = %d\n", foo(atoi(argv[1])));
  return 0;
}
