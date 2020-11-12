// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <gtest/gtest.h>

#include "echo_server.h"

namespace {

class ComponentContextProviderTests : public gtest::RealLoopFixture {
 protected:
  void PublishOutgoingService() {
    ASSERT_EQ(ZX_OK, provider_.context()->outgoing()->AddPublicService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  void PublishIncomingService() {
    ASSERT_EQ(ZX_OK, provider_.service_directory_provider()->AddService(
                         echo_impl_.GetHandler(dispatcher())));
  }

  EchoImpl echo_impl_;
  sys::testing::ComponentContextProvider provider_;
};

TEST_F(ComponentContextProviderTests, TestOutgoingPublicServices) {
  PublishOutgoingService();

  auto echo = provider_.ConnectToPublicService<test::placeholders::Echo>();

  std::string result;
  echo->EchoString("hello", [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);

  // Also try and connect using service directory
  echo = provider_.public_service_directory()->Connect<test::placeholders::Echo>();

  result = "";
  echo->EchoString("hello", [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

TEST_F(ComponentContextProviderTests, TestIncomingServices) {
  PublishIncomingService();

  test::placeholders::EchoPtr echo;

  auto services = provider_.service_directory_provider()->service_directory();

  services->Connect(echo.NewRequest());

  std::string result;
  echo->EchoString("hello", [&result](fidl::StringPtr value) { result = *value; });

  RunLoopUntilIdle();
  EXPECT_EQ("hello", result);
}

}  // namespace
