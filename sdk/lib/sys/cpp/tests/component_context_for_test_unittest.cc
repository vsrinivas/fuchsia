// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/component_context_for_test.h>

#include "echo_server.h"

#include <lib/gtest/real_loop_fixture.h>

#include "gtest/gtest.h"

namespace {

class ComponentContextForTest_Tests : public gtest::RealLoopFixture {
 protected:
  void PublishOutgoingService(sys::ComponentContext* context) {
    ASSERT_EQ(ZX_OK, context->outgoing().AddPublicService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  void PublishIncomingService(sys::testing::ComponentContextForTest* context) {
    ASSERT_EQ(ZX_OK, context->service_directory_for_test()->AddService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  EchoImpl echo_impl_;
};

TEST_F(ComponentContextForTest_Tests, TestOutgoingPublicServices) {
  auto context = sys::testing::ComponentContextForTest::Create();

  PublishOutgoingService(context.get());

  auto echo = context->ConnectToPublicService<fidl::examples::echo::Echo>();

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

TEST_F(ComponentContextForTest_Tests, TestIncomingServices) {
  auto context = sys::testing::ComponentContextForTest::Create();

  PublishIncomingService(context.get());

  fidl::examples::echo::EchoPtr echo;

  context->svc()->Connect(echo.NewRequest());

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

}  // namespace