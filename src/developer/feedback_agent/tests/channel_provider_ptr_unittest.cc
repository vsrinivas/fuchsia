// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/feedback_agent/channel_provider_ptr.h"

#include <lib/async_promise/executor.h>
#include <lib/fit/single_threaded_executor.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>

#include <memory>
#include <string>

#include "src/developer/feedback_agent/tests/stub_channel_provider.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/test/test_settings.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace fuchsia {
namespace feedback {
namespace {

class RetrieveCurrentChannelTest : public gtest::RealLoopFixture {
 public:
  RetrieveCurrentChannelTest()
      : executor_(dispatcher()), service_directory_provider_(dispatcher()) {}

 protected:
  void ResetChannelProvider(std::unique_ptr<StubUpdateInfo> stub_channel_provider) {
    stub_channel_provider_ = std::move(stub_channel_provider);
    if (stub_channel_provider_) {
      FXL_CHECK(service_directory_provider_.AddService(stub_channel_provider_->GetHandler()) ==
                ZX_OK);
    }
  }

  fit::result<std::string> RetrieveCurrentChannel(const zx::duration timeout = zx::sec(1)) {
    fit::result<std::string> result;
    executor_.schedule_task(
        fuchsia::feedback::RetrieveCurrentChannel(
            dispatcher(), service_directory_provider_.service_directory(), timeout)
            .then([&result](fit::result<std::string>& res) { result = std::move(res); }));
    RunLoopUntil([&result] { return !!result; });
    return result;
  }

 private:
  async::Executor executor_;
  ::sys::testing::ServiceDirectoryProvider service_directory_provider_;

  std::unique_ptr<StubUpdateInfo> stub_channel_provider_;
};

TEST_F(RetrieveCurrentChannelTest, Succeed_SomeChannel) {
  std::unique_ptr<StubUpdateInfo> stub_channel_provider = std::make_unique<StubUpdateInfo>();
  stub_channel_provider->set_channel("my-channel");
  ResetChannelProvider(std::move(stub_channel_provider));

  fit::result<std::string> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_ok());
  EXPECT_STREQ(result.take_value().c_str(), "my-channel");
}

TEST_F(RetrieveCurrentChannelTest, Succeed_EmptyChannel) {
  ResetChannelProvider(std::make_unique<StubUpdateInfo>());

  fit::result<std::string> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_ok());
  EXPECT_STREQ(result.take_value().c_str(), "");
}

TEST_F(RetrieveCurrentChannelTest, Fail_ChannelProviderNotAvailable) {
  ResetChannelProvider(nullptr);

  fit::result<std::string> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_error());
}

TEST_F(RetrieveCurrentChannelTest, Fail_ChannelProviderClosesConnection) {
  ResetChannelProvider(std::make_unique<StubUpdateInfoClosesConnection>());

  fit::result<std::string> result = RetrieveCurrentChannel();

  ASSERT_TRUE(result.is_error());
}

TEST_F(RetrieveCurrentChannelTest, Fail_ChannelProviderNeverReturns) {
  ResetChannelProvider(std::make_unique<StubUpdateInfoNeverReturns>());

  fit::result<std::string> result = RetrieveCurrentChannel(/*timeout=*/zx::msec(10));

  ASSERT_TRUE(result.is_error());
}

}  // namespace
}  // namespace feedback
}  // namespace fuchsia

int main(int argc, char** argv) {
  if (!fxl::SetTestSettings(argc, argv)) {
    return EXIT_FAILURE;
  }

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"feedback", "test"});
  return RUN_ALL_TESTS();
}
