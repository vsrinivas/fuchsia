// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

#include <string>

constexpr int kLimit = 20;

void Print(int i) {
  printf("Iteration %d/%d\n", i + 1, kLimit);
  fflush(stdout);
}

int main(int argc, char* argv[]) {
  int iterations = kLimit;
  if (argc == 2)
    iterations = std::stoi(argv[1]);

  // Run for 20 seconds and then end.
  for (int i = 0; i < iterations; i++) {
    Print(i);
    sleep(1);
  }
}
