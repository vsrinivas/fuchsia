// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/integration_testing/cpp/reporting.h"

#include <lib/integration_testing/cpp/testing.h>

namespace modular {
namespace testing {

TestPoint::TestPoint(std::string label) : label_(std::move(label)) {
  RegisterTestPoint(label_);
}

TestPoint::~TestPoint() {
  if (!value_)
    TEST_FAIL(label_);
}

void TestPoint::Pass() {
  value_ = true;
  TEST_PASS(label_);
  PassTestPoint(label_);
}

}  // namespace testing
}  // namespace modular
