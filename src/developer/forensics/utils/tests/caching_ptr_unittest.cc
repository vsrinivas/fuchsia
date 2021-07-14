// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/caching_ptr.h"

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace fidl {
namespace {

class CachingChannelPtr {
 public:
  CachingChannelPtr(async_dispatcher_t* dispatcher, std::shared_ptr<sys::ServiceDirectory> services)
      : connection_(dispatcher, services, [this] { GetChannel(); }) {}

  ::fpromise::promise<std::string, Error> GetChannel(zx::duration timeout) {
    return connection_.GetValue(fit::Timeout(timeout));
  }

 private:
  void GetChannel() {
    connection_->GetCurrent([this](std::string channel) {
      if (!channel.empty()) {
        connection_.SetValue(channel);
      } else {
        connection_.SetError(Error::kMissingValue);
      }
    });
  }

  CachingPtr<fuchsia::update::channelcontrol::ChannelControl, std::string> connection_;
};

constexpr char kChannel[] = "my-channel";
constexpr zx::duration kTimeout = zx::sec(1);

// We need to use an actual FIDL interface to test CachingPtr, so we use
// fuchsia::update::channelcontrol::ChannelControl and stubs::ChannelControl in our test cases.
class CachingPtrTest : public UnitTestFixture {
 public:
  CachingPtrTest() : executor_(dispatcher()), channel_provider_server_() {}

 protected:
  template <typename V, typename E>
  ::fpromise::result<V, E> ExecutePromise(::fpromise::promise<V, E> promise) {
    ::fpromise::result<V, E> out_result;
    executor_.schedule_task(std::move(promise).then(
        [&](::fpromise::result<V, E>& result) { out_result = std::move(result); }));
    RunLoopFor(kTimeout);
    return out_result;
  }

  void SetUpChannelProviderServer(
      std::unique_ptr<stubs::ChannelControlBase> channel_provider_server) {
    channel_provider_server_ = std::move(channel_provider_server);
    if (channel_provider_server_) {
      InjectServiceProvider(channel_provider_server_.get());
    }
  }

 private:
  async::Executor executor_;
  std::unique_ptr<stubs::ChannelControlBase> channel_provider_server_;
};

TEST_F(CachingPtrTest, Check_CachesValueInConstructor) {
  CachingChannelPtr channel_ptr(dispatcher(), services());
  SetUpChannelProviderServer(
      std::make_unique<stubs::ChannelControlExpectsOneCall>(stubs::ChannelControlBase::Params{
          .current = kChannel,
          .target = std::nullopt,
      }));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto channel_result = ExecutePromise(channel_ptr.GetChannel(kTimeout));

    ASSERT_TRUE(channel_result.is_ok());
    EXPECT_EQ(channel_result.value(), kChannel);
  }
}

TEST_F(CachingPtrTest, Check_CachesErrorInConstructor) {
  CachingChannelPtr channel_ptr(dispatcher(), services());
  SetUpChannelProviderServer(
      std::make_unique<stubs::ChannelControlExpectsOneCall>(stubs::ChannelControlBase::Params{
          .current = "",
          .target = std::nullopt,
      }));

  RunLoopUntilIdle();

  for (size_t i = 0; i < 10; ++i) {
    const auto channel_result = ExecutePromise(channel_ptr.GetChannel(kTimeout));

    ASSERT_TRUE(channel_result.is_error());
    EXPECT_EQ(channel_result.error(), Error::kMissingValue);
  }
}

TEST_F(CachingPtrTest, Check_ErrorOnTimeout) {
  CachingChannelPtr channel_ptr(dispatcher(), services());

  SetUpChannelProviderServer(std::make_unique<stubs::ChannelControlNeverReturns>());

  const auto channel_result = ExecutePromise(channel_ptr.GetChannel(kTimeout));

  ASSERT_TRUE(channel_result.is_error());
  EXPECT_EQ(channel_result.error(), Error::kTimeout);
}

TEST_F(CachingPtrTest, Check_SuccessOnSecondAttempt) {
  CachingChannelPtr channel_ptr(dispatcher(), services());
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelControlClosesFirstConnection>(
      stubs::ChannelControlBase::Params{
          .current = kChannel,
          .target = std::nullopt,
      }));

  RunLoopUntilIdle();

  // We set the timeout to be larger than the backoff so we're guarenteed to have a value
  auto channel_result = ExecutePromise(channel_ptr.GetChannel(zx::sec(1)));

  ASSERT_TRUE(channel_result.is_ok());
  EXPECT_EQ(channel_result.value(), kChannel);
}

TEST_F(CachingPtrTest, Check_ReturnErrorOnNoServer) {
  CachingChannelPtr channel_ptr(dispatcher(), services());

  SetUpChannelProviderServer(nullptr);

  const auto channel_result = ExecutePromise(channel_ptr.GetChannel(kTimeout));

  ASSERT_TRUE(channel_result.is_error());
  EXPECT_EQ(channel_result.error(), Error::kTimeout);
}

}  // namespace
}  // namespace fidl
}  // namespace forensics
