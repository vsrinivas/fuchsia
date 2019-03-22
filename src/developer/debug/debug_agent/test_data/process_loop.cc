// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdio.h>
#include <unistd.h>

constexpr int kLimit = 20;

int main() {
  // Run for 20 seconds and then end.
  for (int i = 0; i < kLimit; i++) {
    printf("Iteration %d/%d\n", i + 1, kLimit);
    sleep(1);
  }
}
