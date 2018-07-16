// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cloud_provider/validation/validation_test.h>

namespace cloud_provider {

ValidationTest::ValidationTest()
    : startup_context_(
          component::StartupContext::CreateFromStartupInfoNotChecked()) {}
ValidationTest::~ValidationTest() {}

void ValidationTest::SetUp() {
  startup_context_->ConnectToEnvironmentService(cloud_provider_.NewRequest());
}

}  // namespace cloud_provider
