// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "helper.h"

#include <cstdio>

#include <zxtest/cpp/zxtest.h>

int main(int argc, char** argv) {
  zxtest::RunAllTests(argc, argv);
  zxtest::test::CheckAll();
  fprintf(stdout,
          "All Checks Passed. Assertion errors displayed in the standard output is from "
          "validating the assertion mechanisms.\n ");
  return 0;
}
