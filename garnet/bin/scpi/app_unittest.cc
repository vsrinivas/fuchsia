// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/scpi/app.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include "gtest/gtest.h"

namespace scpi {
namespace testing {

using namespace fuchsia::scpi;

class AppTest : public gtest::TestLoopFixture {
 protected:
  AppTest() : app_(std::make_unique<App>(context_provider_.TakeContext())) {
    app_->Start();
  }

  SystemControllerPtr GetSystemController() {
    SystemControllerPtr system_controller;
    context_provider_.ConnectToPublicService(system_controller.NewRequest());
    return system_controller;
  }

 private:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<App> app_;
};

// GetDvfsInfo
TEST_F(AppTest, GetDvfsInfo) {
  SystemControllerPtr scpi_ = GetSystemController();
  Status st;
  int size;
  scpi_->GetDvfsInfo(0, [&](Status err, std::vector<DvfsOpp> opps) {
    st = err;
    size = (int)opps.size();
  });
  RunLoopUntilIdle();
  EXPECT_EQ(fuchsia::scpi::Status::OK, st);
  EXPECT_NE(0, size);
}

// GetSystemStatus
TEST_F(AppTest, GetSystemStatus) {
  SystemControllerPtr scpi_ = GetSystemController();
  Status st;
  int temp;
  scpi_->GetSystemStatus([&](Status err, SystemStatus sys_status) {
    st = err;
    temp = sys_status.temperature;
  });
  RunLoopUntilIdle();
  EXPECT_EQ(fuchsia::scpi::Status::OK, st);
  EXPECT_NE(0, temp);
}

}  // namespace testing
}  // namespace scpi
