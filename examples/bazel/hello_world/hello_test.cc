// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>

#include <iostream>

// Demonstrate some basic assertions.
int main() {
  std::cout << "Example stdout." << std::endl;

  // Expect two strings not to be equal.
  std::string a("hello");
  std::string b("world");
  assert(a != b);

  // Expect equality.
  assert(7 * 6 == 42);

  return 0;
}
