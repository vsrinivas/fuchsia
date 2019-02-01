// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>

#include "gperftools/profiler.h"

void words_tests();
void magic_numbers_tests();

int main() {
  printf("Starting profiling tests...\n");
  ProfilerStart("/tmp/profiler_example.ppf");

  // Run some fun benchmarks
  magic_numbers_tests();
  words_tests();

  ProfilerStop();
  printf("...Done with profiling tests\n");
  return 0;
}
