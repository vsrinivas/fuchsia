// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/public/lib/cloud_provider/validation/validation_test.h"

namespace cloud_provider {

ValidationTest::ValidationTest()
    : application_context_(
          app::ApplicationContext::CreateFromStartupInfoNotChecked()) {}
ValidationTest::~ValidationTest() {}

void ValidationTest::SetUp() {
  fidl::InterfacePtr<CloudProvider> cloud_provider =
      application_context_->ConnectToEnvironmentService<CloudProvider>();
  cloud_provider_ = CloudProviderPtr::Create(std::move(cloud_provider));

  Status status = Status::INTERNAL_ERROR;
  cloud_provider_->EraseAllData(
      [&status](Status got_status) { status = got_status; });
  ASSERT_TRUE(cloud_provider_.WaitForIncomingResponse());
  EXPECT_EQ(Status::OK, status);
}

}  // namespace cloud_provider
