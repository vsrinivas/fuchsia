// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/reporting.h"

TestPoint::TestPoint(std::string label)
: label_(std::move(label)) {}

TestPoint::~TestPoint() {
  if (!value_)
    TEST_FAIL(label_);
}

void
TestPoint::Pass() {
  value_ = true;
  TEST_PASS(label_);
}
