// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/cloud_provider/fidl/cloud_provider.fidl.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"

namespace cloud_provider {
namespace {

class ValidationTest : public ::testing::Test {
 public:
  ValidationTest()
      : application_context_(
            app::ApplicationContext::CreateFromStartupInfoNotChecked()) {}
  ~ValidationTest() override {}

 protected:
  CloudProviderPtr GetCloudProvider() {
    fidl::InterfacePtr<CloudProvider> cloud_provider =
        application_context_->ConnectToEnvironmentService<CloudProvider>();
    return CloudProviderPtr::Create(std::move(cloud_provider));
  }

 private:
  fsl::MessageLoop message_loop_;
  std::unique_ptr<app::ApplicationContext> application_context_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ValidationTest);
};

class DeviceSetTest : public ValidationTest {
 public:
  DeviceSetTest() {}
  ~DeviceSetTest() override {}
};

TEST_F(DeviceSetTest, GetDeviceSet) {
  Status got_status = Status::INTERNAL_ERROR;
  CloudProviderPtr cloud_provider = GetCloudProvider();
  DeviceSetPtr device_set;
  cloud_provider->GetDeviceSet(
      device_set.NewRequest(),
      [&got_status](Status status) { got_status = status; });
  cloud_provider.WaitForIncomingResponse();
  EXPECT_EQ(Status::OK, got_status);
}

}  // namespace
}  // namespace cloud_provider
