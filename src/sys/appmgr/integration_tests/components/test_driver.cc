// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

constexpr char kTestFile[] = "/dev/class/usb-device/test.txt";

int main(int argc, const char** argv) {
  if (fopen(kTestFile, "r") == nullptr) {
    printf("Failed to open file %s: %s", kTestFile, strerror(errno));
    return 1;
  }

  return 0;
}
