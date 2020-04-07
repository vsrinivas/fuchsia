// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_agent/annotations/channel_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/promise.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include "src/developer/feedback/feedback_agent/annotations/aliases.h"
#include "src/developer/feedback/testing/cobalt_test_fixture.h"
#include "src/developer/feedback/testing/stubs/channel_provider.h"
#include "src/developer/feedback/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt_event.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/googletest/googlemock/include/gmock/gmock.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace feedback {
namespace {

using testing::UnorderedElementsAreArray;

class ChannelProviderTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  ChannelProviderTest() : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelProvider> channel_provider_server) {
    channel_provider_server_ = std::move(channel_provider_server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  std::optional<std::string> GetCurrentChannel(const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltLoggerFactory(std::make_unique<stubs::CobaltLoggerFactory>());
    Cobalt cobalt(dispatcher(), services());

    ChannelProvider provider(dispatcher(), services(), timeout, &cobalt);
    auto promise = provider.GetAnnotations();

    bool was_called = false;
    std::optional<std::string> channel;
    executor_.schedule_task(
        std::move(promise).then([&was_called, &channel](::fit::result<Annotations>& res) {
          was_called = true;

          if (res.is_error()) {
            channel = std::nullopt;
          } else {
            Annotations annotations = res.take_value();
            if (annotations.empty()) {
              channel = std::nullopt;
            } else {
              FX_CHECK(annotations.size() == 1u);
              channel = std::move(annotations.begin()->second);
            }
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

TEST_F(ChannelProviderTest, Succeed_SomeChannel) {
  auto channel_provider = std::make_unique<stubs::ChannelProvider>();
  channel_provider->set_channel("my-channel");
  SetUpChannelProviderServer(std::move(channel_provider));

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "my-channel");
}

TEST_F(ChannelProviderTest, Succeed_EmptyChannel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProvider>());

  const auto result = GetCurrentChannel();

  ASSERT_TRUE(result);
  EXPECT_EQ(result.value(), "");
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerNotAvailable) {
  SetUpChannelProviderServer(nullptr);

  const auto result = GetCurrentChannel();

  ASSERT_FALSE(result);
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerClosesConnection) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderClosesConnection>());

  const auto result = GetCurrentChannel();

  ASSERT_FALSE(result);
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerNeverReturns) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderNeverReturns>());

  const auto result = GetCurrentChannel();

  ASSERT_FALSE(result);
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          CobaltEvent(TimedOutData::kChannel),
                                      }));
}

}  // namespace
}  // namespace feedback
