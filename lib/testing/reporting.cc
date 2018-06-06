// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/reporting.h"

namespace modular {
namespace testing {

TestPoint::TestPoint(std::string label) : label_(std::move(label)) {
  internal::RegisterTestPoint(label_);
}

TestPoint::~TestPoint() {
  if (!value_)
    TEST_FAIL(label_);
}

void TestPoint::Pass() {
  value_ = true;
  TEST_PASS(label_);
  internal::PassTestPoint(label_);
}

}  // namespace testing
}  // namespace modular
