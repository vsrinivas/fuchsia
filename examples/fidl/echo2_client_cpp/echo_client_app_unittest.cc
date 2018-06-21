// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_client_app.h"
#include "lib/app/cpp/testing/fake_component.h"
#include "lib/app/cpp/testing/test_with_context.h"

namespace echo2 {
namespace testing {

using namespace fidl::examples::echo;

// Fake server, which the client under test will be used against
class FakeEcho : public Echo {
 public:
  static const std::string kURL_;

  FakeEcho() { component_.AddPublicService(bindings_.GetHandler(this)); }

  // Fake implementation of server-side logic
  void EchoString(fidl::StringPtr value, EchoStringCallback callback) {
    callback(answer_);
  }

  void SetAnswer(fidl::StringPtr answer) { answer_ = answer; }

  // Register to be launched with a fake URL
  void Register(fuchsia::sys::testing::FakeLauncher& fake_launcher) {
    component_.Register(kURL_, fake_launcher);
  }

 private:
  fuchsia::sys::testing::FakeComponent component_;
  fidl::BindingSet<Echo> bindings_;
  fidl::StringPtr answer_;
};

const std::string FakeEcho::kURL_ = "fake-echo";

class EchoClientAppForTest : public EchoClientApp {
 public:
  // Expose injecting constructor so we can pass an instrumented Context
  EchoClientAppForTest(std::unique_ptr<fuchsia::sys::StartupContext> context)
      : EchoClientApp(std::move(context)) {}
};

class EchoClientAppTest : public fuchsia::sys::testing::TestWithContext {
 public:
  void SetUp() override {
    TestWithContext::SetUp();
    echoClientApp_.reset(new EchoClientAppForTest(TakeContext()));
    fake_echo_.reset(new FakeEcho());
    fake_echo_->Register(controller().fake_launcher());
  }

  void TearDown() override {
    echoClientApp_.reset();
    TestWithContext::TearDown();
  }

 protected:
  void Start(std::string server_url) { echoClientApp_->Start(server_url); }
  EchoPtr& echo() { return echoClientApp_->echo(); }
  void SetAnswer(fidl::StringPtr answer) { fake_echo_->SetAnswer(answer); }

 private:
  std::unique_ptr<EchoClientAppForTest> echoClientApp_;
  std::unique_ptr<FakeEcho> fake_echo_;
};

// Answer "Hello World" with "Goodbye World"
TEST_F(EchoClientAppTest, EchoString_HelloWorld_GoodbyeWorld) {
  fidl::StringPtr message = "bogus";
  Start(FakeEcho::kURL_);
  SetAnswer("Goodbye World!");
  echo()->EchoString("Hello World!",
                     [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("Goodbye World!", message);
}

// Talking to remote without starting it first doesn't work
TEST_F(EchoClientAppTest, EchoString_NoStart) {
  fidl::StringPtr message = "bogus";
  echo()->EchoString("Hello World!",
                     [&](::fidl::StringPtr retval) { message = retval; });
  RunLoopUntilIdle();
  EXPECT_EQ("bogus", message);
}

}  // namespace testing
}  // namespace echo2
