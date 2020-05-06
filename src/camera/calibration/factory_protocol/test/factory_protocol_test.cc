// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/calibration/factory_protocol/factory_protocol.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <src/lib/files/directory.h>
#include <src/lib/files/file.h>
#include <src/lib/testing/loop_fixture/test_loop_fixture.h>

namespace camera {
namespace {

class FactoryServerTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    auto channel = factory_protocol_.NewRequest().TakeChannel();
    factory_protocol_.set_error_handler([&](zx_status_t status) {
      if (status != ZX_OK)
        ADD_FAILURE() << "Channel Failure: " << status;
    });

    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;

    auto result = FactoryServer::Create(std::move(controller));
    ASSERT_TRUE(result.is_ok());

    factory_server_ = std::move(result.value());
    ASSERT_FALSE(factory_server_->streaming());
  }

  void TearDown() override {
    factory_protocol_ = nullptr;
    factory_server_ = nullptr;
    RunLoopUntilIdle();
  }

  std::unique_ptr<FactoryServer> factory_server_;
  fuchsia::factory::camera::CameraFactoryPtr factory_protocol_;
};

TEST_F(FactoryServerTest, DummyNoOp) { ASSERT_TRUE(true); }

}  // namespace
}  // namespace camera
