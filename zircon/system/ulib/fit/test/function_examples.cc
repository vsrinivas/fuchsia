// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "examples/function_example1.h"
#include "examples/function_example2.h"

namespace {

bool example1() {
  BEGIN_TEST;
  function_example1::run();
  END_TEST;
}

bool example2() {
  BEGIN_TEST;
  function_example2::run();
  END_TEST;
}
}  // namespace

BEGIN_TEST_CASE(function_examples)
RUN_TEST(example1);
RUN_TEST(example2);
END_TEST_CASE(function_examples)
