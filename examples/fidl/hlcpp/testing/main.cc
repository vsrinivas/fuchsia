// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START includes]
#include <fuchsia/examples/cpp/fidl.h>
#include <fuchsia/examples/cpp/fidl_test_base.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
// [END includes]

// [START impl]
class EchoImpl : public fuchsia::examples::testing::Echo_TestBase {
 public:
  void EchoString(std::string value, EchoStringCallback callback) override { callback(value); }
  void NotImplemented_(const std::string& name) override {
    std::cout << "Not implemented: " << name << std::endl;
  }
};
// [END impl]

// [START wrapper]
class EchoServerInstance {
 public:
  explicit EchoServerInstance(std::unique_ptr<sys::ComponentContext> context) {
    context_ = std::move(context);
    binding_ = std::make_unique<fidl::Binding<fuchsia::examples::Echo>>(&impl_);
    fidl::InterfaceRequestHandler<fuchsia::examples::Echo> handler =
        [&](fidl::InterfaceRequest<fuchsia::examples::Echo> request) {
          binding_->Bind(std::move(request));
        };
    context_->outgoing()->AddPublicService(std::move(handler));
  }

 private:
  EchoImpl impl_;
  std::unique_ptr<fidl::Binding<fuchsia::examples::Echo>> binding_;
  std::unique_ptr<sys::ComponentContext> context_;
};
// [END wrapper]

// [START fixture]
class EchoTestFixture : public gtest::TestLoopFixture {
 public:
  void SetUp() override {
    TestLoopFixture::SetUp();
    echo_instance_.reset(new EchoServerInstance(provider_.TakeContext()));
  }

  void TearDown() override {
    TestLoopFixture::TearDown();
    echo_instance_.reset();
  }

 protected:
  fuchsia::examples::EchoPtr GetProxy() {
    fuchsia::examples::EchoPtr echo;
    provider_.ConnectToPublicService(echo.NewRequest());
    return echo;
  }

 private:
  std::unique_ptr<EchoServerInstance> echo_instance_;
  sys::testing::ComponentContextProvider provider_;
};
// [END fixture]

// [START test]
TEST_F(EchoTestFixture, EchoString) {
  fuchsia::examples::EchoPtr proxy = GetProxy();
  proxy->EchoString("hello there",
                    [&](std::string response) { ASSERT_EQ(response, "hello there"); });
  RunLoopUntilIdle();
}
// [END test]
