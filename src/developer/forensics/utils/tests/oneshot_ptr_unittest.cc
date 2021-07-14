// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl/oneshot_ptr.h"

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace fidl {
namespace {

constexpr char kChannel[] = "my-channel";
constexpr zx::duration kTimeout = zx::sec(1);

// We need to use an actual FIDL interface to test OneShotPtr, so we use
// fuchsia::update::channelcontrol::ChannelControl and stubs::ChannelControl in our test cases.
class OneShotPtrTest : public UnitTestFixture {
 public:
  OneShotPtrTest() : executor_(dispatcher()), channel_provider_server_() {}

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

TEST_F(OneShotPtrTest, Check_Success) {
  auto channel_provider = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannel,
      .target = std::nullopt,
  });

  SetUpChannelProviderServer(std::move(channel_provider));

  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl, std::string> channel_ptr(dispatcher(),
                                                                                       services());

  channel_ptr->GetCurrent([&](std::string channel) {
    if (channel_ptr.IsAlreadyDone()) {
      return;
    }

    channel_ptr.CompleteOk(channel);
  });

  auto result = ExecutePromise(channel_ptr.WaitForDone());
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.value(), kChannel);
}

TEST_F(OneShotPtrTest, Fail_NoServer) {
  SetUpChannelProviderServer(nullptr);
  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl> channel_ptr(dispatcher(), services());

  // Make a call to ensure we connect to the server.
  channel_ptr->GetCurrent([&](std::string channel) {});

  const auto result = ExecutePromise(channel_ptr.WaitForDone());
  EXPECT_TRUE(result.is_error());
}

TEST_F(OneShotPtrTest, Fail_ClosedChannel) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelControlClosesConnection>());
  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl> channel_ptr(dispatcher(), services());

  // Make a call to ensure we connect to the server.
  channel_ptr->GetCurrent([&](std::string channel) {});

  const auto result = ExecutePromise(channel_ptr.WaitForDone());
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), Error::kConnectionError);
}

TEST_F(OneShotPtrTest, Fail_Timeout) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelControlNeverReturns>());
  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl> channel_ptr(dispatcher(), services());

  bool did_timeout = false;
  const auto result = ExecutePromise(
      channel_ptr.WaitForDone(fit::Timeout(kTimeout, [&did_timeout]() { did_timeout = true; })));

  ASSERT_TRUE(result.is_error());
  EXPECT_TRUE(did_timeout);
}

TEST_F(OneShotPtrTest, Crash_MultipleUses) {
  auto channel_provider = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannel,
      .target = std::nullopt,
  });

  SetUpChannelProviderServer(std::move(channel_provider));

  OneShotPtr<fuchsia::update::channelcontrol::ChannelControl> channel_ptr(dispatcher(), services());

  channel_ptr->GetCurrent([&](std::string channel) {});

  ASSERT_DEATH({ channel_ptr->GetCurrent([](std::string channel) {}); },
               testing::HasSubstr("You've only got one shot to use ->  on a OneShotPtr"));
}

}  // namespace
}  // namespace fidl
}  // namespace forensics
