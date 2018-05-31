// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/cloud_provider/validation/validation_test.h"

namespace cloud_provider {

ValidationTest::ValidationTest()
    : startup_context_(
          component::StartupContext::CreateFromStartupInfoNotChecked()) {}
ValidationTest::~ValidationTest() {}

void ValidationTest::SetUp() {
  fidl::InterfacePtr<CloudProvider> cloud_provider =
      startup_context_->ConnectToEnvironmentService<CloudProvider>();
  cloud_provider_ = std::move(cloud_provider);
}

}  // namespace cloud_provider
