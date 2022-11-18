// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unistd.h>

#include <iostream>
#include <set>

uint64_t fibbonacci(uint64_t n, std::set<uint64_t> *values) {
  if (n == 0) {
    return 0;
  }
  if (n == 1) {
    return 1;
  }
  uint64_t result = (fibbonacci(n - 1, values) + fibbonacci(n - 2, values));
  values->insert(result);
  return result;
}

int main() {
  std::cout << "Hello, World!\n" << std::endl << std::flush;

  for (int i = 0; i < 32; i++) {
    std::set<uint64_t> values;
    std::cout << "Compute fibbonacci(" << i << ")" << std::endl << std::flush;
    usleep(100000);
    fibbonacci(i, &values);
  }

  std::cout << "Bye bye.\n" << std::endl << std::flush;
  return 0;
}
