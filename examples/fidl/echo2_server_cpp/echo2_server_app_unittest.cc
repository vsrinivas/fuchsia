// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo2_server_app.h"
#include "lib/app/cpp/testing/startup_context_for_test.h"
#include "lib/gtest/test_with_loop.h"

namespace echo2 {
namespace testing {

class EchoServerAppForTest : public EchoServerApp {
 public:
  EchoServerAppForTest(std::unique_ptr<fuchsia::sys::StartupContext> context)
      : EchoServerApp(std::move(context)) {}
};

class EchoServerAppTest : public ::gtest::TestWithLoop {
 public:
  void SetUp() override {
    TestWithLoop::SetUp();
    auto context = fuchsia::sys::testing::StartupContextForTest::Create();
    services_ = &context->services();
    echoServerApp_.reset(new EchoServerAppForTest(std::move(context)));
  }

  void TearDown() override {
    echoServerApp_.reset();
    TestWithLoop::TearDown();
  }

 protected:
  fidl::examples::echo::EchoPtr echo() {
    fidl::examples::echo::EchoPtr echo;
    services_->ConnectToService(echo.NewRequest());
    return echo;
  }

 private:
  std::unique_ptr<EchoServerAppForTest> echoServerApp_;
  const fuchsia::sys::Services* services_;
};

// Answer "Hello World" with "Hello World"
TEST_F(EchoServerAppTest, EchoString_HelloWorld) {
  fidl::examples::echo::EchoPtr echo_ = echo();
  ::fidl::StringPtr message = "bogus";
  echo_->EchoString("Hello World!",
                    [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Hello World!", message);
}

// Answer "" with ""
TEST_F(EchoServerAppTest, EchoString_Empty) {
  fidl::examples::echo::EchoPtr echo_ = echo();
  ::fidl::StringPtr message = "bogus";
  echo_->EchoString("", [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("", message);
}

}  // namespace testing
}  // namespace echo2
