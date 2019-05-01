// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

// Simple program that writes both to stdout and stderr.
// It also calls some global variables, which is useful for manually testing
// features like watchpoints.

int global_int = 0;

void SomeFunction() {
  global_int++;
  printf("Some function!\n");
}

using FunctionPtr = void (*)(void);

int main() {
  FunctionPtr ptr = SomeFunction;
  ptr();

  // Touch the global integer.
  global_int++;
  printf("Writing into stdout. Global int: %d.\n", global_int++);
  fprintf(stderr, "Writing into stderr. Global int: %d.\n", global_int++);
}
