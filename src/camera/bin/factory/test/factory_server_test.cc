// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/bin/factory/factory_server.h"

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
    // TODO(fxbug.dev/58025): This is a hack for testing this dummy.
    auto streamer = std::make_unique<Streamer>();
    auto result = FactoryServer::Create(std::move(streamer), [] {});
    ASSERT_TRUE(result.is_ok());
    factory_server_ = std::move(result.value());
  }

  void TearDown() override {
    factory_server_ = nullptr;
    RunLoopUntilIdle();
  }

  std::unique_ptr<FactoryServer> factory_server_;
};

TEST_F(FactoryServerTest, DummyNoOp) { ASSERT_TRUE(true); }

}  // namespace
}  // namespace camera
