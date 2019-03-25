// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_server_app.h"

#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

namespace echo {
namespace testing {

using namespace fidl::examples::echo;

class EchoServerAppForTest : public EchoServerApp {
 public:
  // Expose injecting constructor so we can pass an instrumented Context
  EchoServerAppForTest(std::unique_ptr<sys::ComponentContext> context)
      : EchoServerApp(std::move(context), false) {}
};

class EchoServerAppTest : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    echoServerApp_.reset(new EchoServerAppForTest(provider_.TakeContext()));
  }

  void TearDown() override {
    echoServerApp_.reset();
    TestLoopFixture::TearDown();
  }

 protected:
  EchoPtr echo() {
    EchoPtr echo;
    provider_.ConnectToPublicService(echo.NewRequest());
    return echo;
  }

 private:
  std::unique_ptr<EchoServerAppForTest> echoServerApp_;
  sys::testing::ComponentContextProvider provider_;
};

// Answer "Hello World" with "Hello World"
TEST_F(EchoServerAppTest, EchoString_HelloWorld) {
  EchoPtr echo_ = echo();
  ::fidl::StringPtr message = "bogus";
  echo_->EchoString("Hello World!",
                    [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Hello World!", message);
}

// Answer "" with ""
TEST_F(EchoServerAppTest, EchoString_Empty) {
  EchoPtr echo_ = echo();
  fidl::StringPtr message = "bogus";
  echo_->EchoString("", [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("", message);
}

}  // namespace testing
}  // namespace echo
