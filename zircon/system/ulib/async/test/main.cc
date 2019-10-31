// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>
#include <zxtest/c/zxtest.h>

int main(int argc, char** argv) {
  int zxtest_return_code = RUN_ALL_TESTS(argc, argv);
  const bool ut_ok = unittest_run_all_tests(argc, argv);
  return ut_ok ? zxtest_return_code : EXIT_FAILURE;
}
