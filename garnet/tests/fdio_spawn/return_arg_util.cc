// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string>

int main(int argc, const char* argv[]) {
  if (argc < 2) {
    return -1;
  }
  // Won't handle stoi's exception because:
  // 1) Exceptions are disabled.
  // 2) This is just a basic echo program to be used by a test.
  // 3) The test that calls this code does not feed any invalid inputs.
  return std::stoi(std::string(argv[1]), nullptr, 10);
}
