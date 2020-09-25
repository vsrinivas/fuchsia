// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstring>
#include <iostream>

// Command that fails if given a single argument "fail".
int main(int argc, char** argv) {
  std::cout << "abc\n";
  std::cerr << "xyz\n";
  return (argc > 1 && std::strcmp(argv[1], "fail") == 0);
}
