// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/channel_provider_ptr.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/function.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/channel_provider.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"

namespace forensics {
namespace fidl {
namespace {

class ChannelProviderPtrTest : public UnitTestFixture {
 public:
  ChannelProviderPtrTest() : executor_(dispatcher()) {}

 protected:
  void SetUpChannelProviderServer(
      std::unique_ptr<stubs::ChannelProviderBase> channel_provider_server) {
    channel_provider_server_ = std::move(channel_provider_server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  std::optional<std::string> GetCurrentChannel(::fit::closure if_timeout = [] {}) {
    const zx::duration timeout = zx::sec(1);
    auto promise = fidl::GetCurrentChannel(dispatcher(), services(),
                                           fit::Timeout(timeout, std::move(if_timeout)));

    bool was_called = false;
    std::optional<std::string> channel;
    executor_.schedule_task(
        std::move(promise).then([&was_called, &channel](::fit::result<std::string, Error>& res) {
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
  std::unique_ptr<stubs::ChannelProviderBase> channel_provider_server_;
};

TEST_F(ChannelProviderPtrTest, Succeed_SomeChannel) {
  auto channel_provider = std::make_unique<stubs::ChannelProvider>("my-channel");
  SetUpChannelProviderServer(std::move(channel_provider));

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "my-channel");
}

TEST_F(ChannelProviderPtrTest, Succeed_EmptyChannel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderReturnsEmptyChannel>());

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "");
}

}  // namespace
}  // namespace fidl
}  // namespace forensics
