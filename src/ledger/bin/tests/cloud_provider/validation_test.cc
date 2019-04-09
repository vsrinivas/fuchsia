// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/tests/cloud_provider/validation_test.h"

namespace cloud_provider {

ValidationTest::ValidationTest()
    : component_context_(sys::ComponentContext::Create()) {}
ValidationTest::~ValidationTest() {}

void ValidationTest::SetUp() {
  component_context_->svc()->Connect(cloud_provider_.NewRequest());
}

}  // namespace cloud_provider
