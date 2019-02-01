// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <zircon/syscalls.h>

static void print_time(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  printf("%ld.%06ld\n", (long)tv.tv_sec, (long)tv.tv_usec);
}

// TODO(dje): This just does iterations of null syscalls at the moment,
// but the intent is to add other kinds of syscalls that we need a test
// program for. E.g., programs that block in syscalls to exercise reg access
// of suspended threads.

int main(int argc, char* argv[]) {
  print_time();

  int nr_iterations = 1000;
  if (argc == 2) {
    nr_iterations = atoi(argv[1]);
    if (nr_iterations <= 0) {
      fprintf(stderr, "Invalid # iterations: %s\n", argv[1]);
      return 1;
    }
  }

  for (int i = 0; i < nr_iterations; ++i) {
    zx_syscall_test_0();
  }

  print_time();
  return 0;
}
