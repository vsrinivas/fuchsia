// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/feedback/feedback_data/annotations/channel_provider.h"

#include <lib/async/cpp/executor.h>
#include <lib/fit/promise.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <memory>
#include <optional>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/feedback/feedback_data/annotations/types.h"
#include "src/developer/feedback/feedback_data/constants.h"
#include "src/developer/forensics/testing/cobalt_test_fixture.h"
#include "src/developer/forensics/testing/stubs/channel_provider.h"
#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/event.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace feedback_data {
namespace {

using testing::UnorderedElementsAreArray;

class ChannelProviderTest : public UnitTestFixture, public CobaltTestFixture {
 public:
  ChannelProviderTest() : CobaltTestFixture(/*unit_test_fixture=*/this), executor_(dispatcher()) {}

 protected:
  void SetUpChannelProviderServer(std::unique_ptr<stubs::ChannelProviderBase> server) {
    channel_provider_server_ = std::move(server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

  AnnotationOr GetCurrentChannel(
      const AnnotationKeys& allowlist = {kAnnotationSystemUpdateChannelCurrent},
      const zx::duration timeout = zx::sec(1)) {
    SetUpCobaltServer(std::make_unique<stubs::CobaltLoggerFactory>());
    cobalt::Logger cobalt(dispatcher(), services());

    ChannelProvider provider(dispatcher(), services(), &cobalt);
    auto promise = provider.GetAnnotations(timeout, allowlist);

    bool was_called = false;
    std::optional<AnnotationOr> channel;
    executor_.schedule_task(
        std::move(promise).then([&was_called, &channel](::fit::result<Annotations>& res) {
          was_called = true;

          if (res.is_error()) {
            channel = AnnotationOr(Error::kNotSet);
          } else {
            Annotations annotations = res.take_value();
            if (annotations.empty()) {
              channel = AnnotationOr(Error::kNotSet);
            } else {
              FX_CHECK(annotations.size() == 1u);
              channel = annotations.begin()->second;
            }
          }
        }));
    RunLoopFor(timeout);

    FX_CHECK(was_called);
    FX_CHECK(channel.has_value());

    return channel.value();
  }

  async::Executor executor_;

 private:
  std::unique_ptr<stubs::ChannelProviderBase> channel_provider_server_;
};

TEST_F(ChannelProviderTest, Succeed_SomeChannel) {
  auto channel_provider_server = std::make_unique<stubs::ChannelProvider>("my-channel");
  SetUpChannelProviderServer(std::move(channel_provider_server));

  const auto result = GetCurrentChannel();

  EXPECT_EQ(result, AnnotationOr("my-channel"));
}

TEST_F(ChannelProviderTest, Succeed_EmptyChannel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderReturnsEmptyChannel>());

  const auto result = GetCurrentChannel();

  EXPECT_EQ(result, AnnotationOr(""));
}

TEST_F(ChannelProviderTest, Succeed_NoRequestedKeysInAllowlist) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderReturnsEmptyChannel>());

  const auto result = GetCurrentChannel({"not-returned-by-channel-provider"});

  EXPECT_EQ(result, AnnotationOr(Error::kNotSet));
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerNotAvailable) {
  SetUpChannelProviderServer(nullptr);

  const auto result = GetCurrentChannel();

  EXPECT_EQ(result, AnnotationOr(Error::kConnectionError));
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerClosesConnection) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderClosesConnection>());

  const auto result = GetCurrentChannel();

  EXPECT_EQ(result, AnnotationOr(Error::kConnectionError));
}

TEST_F(ChannelProviderTest, Fail_ChannelProviderServerNeverReturns) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelProviderNeverReturns>());

  const auto result = GetCurrentChannel();

  EXPECT_EQ(result, AnnotationOr(Error::kTimeout));
  EXPECT_THAT(ReceivedCobaltEvents(), UnorderedElementsAreArray({
                                          cobalt::Event(cobalt::TimedOutData::kChannel),
                                      }));
}

}  // namespace
}  // namespace feedback_data
}  // namespace forensics
