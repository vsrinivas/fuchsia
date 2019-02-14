// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sys/cpp/testing/startup_context_for_test.h>

#include "echo_server.h"

#include <lib/fdio/directory.h>
#include <lib/gtest/real_loop_fixture.h>

#include "gtest/gtest.h"

namespace {

class StartupContextForTest_Tests : public gtest::RealLoopFixture {
 protected:
  void PublishOutgoingService(sys::StartupContext* context) {
    ASSERT_EQ(ZX_OK, context->outgoing().AddPublicService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  void PublishIncomingService(sys::testing::StartupContextForTest* context) {
    ASSERT_EQ(ZX_OK, context->service_directory_for_test()->AddService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  EchoImpl echo_impl_;
};

TEST_F(StartupContextForTest_Tests, TestOutgoingPublicServices) {
  auto context = sys::testing::StartupContextForTest::Create();

  PublishOutgoingService(context.get());

  fidl::examples::echo::EchoPtr echo;

  fdio_service_connect_at(
      context->public_directory_ptr().channel().get(),
      fidl::examples::echo::Echo::Name_,
      echo.NewRequest(dispatcher()).TakeChannel().release());

  std::string result;
  echo->EchoString("hello",
                   [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

TEST_F(StartupContextForTest_Tests, TestIncomingServices) {
  auto context = sys::testing::StartupContextForTest::Create();

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