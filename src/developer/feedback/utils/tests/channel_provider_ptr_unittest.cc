// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/utils/fidl/channel_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/feedback/testing/stubs/channel_provider.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace fidl {
namespace {

class ChannelProviderPtrTest : public UnitTestFixture {
 public:
  ChannelProviderPtrTest() : executor_(dispatcher()) {}

 protected:
  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelProvider> channel_provider_server) {
    channel_provider_server_ = std::move(channel_provider_server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  std::optional<std::string> GetCurrentChannel(::fit::closure if_timeout = [] {}) {
    const zx::duration timeout = zx::sec(1);
    auto promise =
        fidl::GetCurrentChannel(dispatcher(), services(), timeout, std::move(if_timeout));

    bool was_called = false;
    std::optional<std::string> channel;
    executor_.schedule_task(
        std::move(promise).then([&was_called, &channel](::fit::result<std::string>& res) {
          was_called = true;

          if (res.is_error()) {
            channel = std::nullopt;
          } else {
            channel = res.take_value();
          }
        }));
    RunLoopFor(timeout);
    FX_CHECK(was_called);
    return channel;
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::ChannelProvider> channel_provider_server_;
};

TEST_F(ChannelProviderPtrTest, Succeed_SomeChannel) {
  auto channel_provider = std::make_unique<stubs::ChannelProvider>();
  channel_provider->set_channel("my-channel");
  SetUpChannelProviderServer(std::move(channel_provider));

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "my-channel");
}

TEST_F(ChannelProviderPtrTest, Succeed_EmptyChannel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>());

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "");
}

TEST_F(ChannelProviderPtrTest, Fail_ChannelProviderPtrNotAvailable) {
  SetUpChannelProviderServer(nullptr);

  const auto result = GetCurrentChannel();

  ASSERT_FALSE(result);
}

TEST_F(ChannelProviderPtrTest, Fail_ChannelProviderPtrClosesConnection) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderClosesConnection>());

  const auto result = GetCurrentChannel();

  ASSERT_FALSE(result);
}

TEST_F(ChannelProviderPtrTest, Fail_ChannelProviderPtrNeverReturns) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderNeverReturns>());

  bool timeout = false;
  const auto result = GetCurrentChannel([&timeout]() { timeout = true; });

  ASSERT_FALSE(result);
  EXPECT_TRUE(timeout);
}

TEST_F(ChannelProviderPtrTest, Fail_CallGetCurrentTwice) {
  const zx::duration unused_timeout = zx::sec(1);
  ChannelProviderPtr ptr(dispatcher(), services());
  executor_.schedule_task(ptr.GetCurrentChannel(unused_timeout));
  ASSERT_DEATH(ptr.GetCurrentChannel(unused_timeout),
               testing::HasSubstr("GetCurrentChannel() is not intended to be called twice"));
}

}  // namespace
}  // namespace fidl
}  // namespace feedback
