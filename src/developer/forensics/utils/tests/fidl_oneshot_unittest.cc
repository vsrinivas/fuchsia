// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/utils/fidl_oneshot.h"

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/async/cpp/executor.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/errors.h"

namespace forensics {
namespace {

constexpr char kChannel[] = "my-channel";
constexpr zx::duration kTimeout = zx::sec(1);

// We need to use an actual FIDL interface to test OneShotCall, so we use
// fuchsia::update::channelcontrol::ChannelControl and stubs::ChannelControl in our test cases.
class OneShotCallTest : public UnitTestFixture {
 public:
  OneShotCallTest() : executor_(dispatcher()), channel_provider_server_() {}

 protected:
  ::fpromise::result<std::string, Error> Run(::fpromise::promise<std::string, Error> promise) {
    ::fpromise::result<std::string, Error> out_result;
    executor_.schedule_task(std::move(promise).then(
        [&](::fpromise::result<std::string, Error>& result) { out_result = std::move(result); }));
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

TEST_F(OneShotCallTest, Check_Success) {
  auto channel_provider = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannel,
      .target = std::nullopt,
  });
  SetUpChannelProviderServer(std::move(channel_provider));

  const auto result = Run(OneShotCall<fuchsia::update::channelcontrol::ChannelControl,
                                      &fuchsia::update::channelcontrol::ChannelControl::GetCurrent>(
      dispatcher(), services(), kTimeout));
  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.value(), kChannel);
}

TEST_F(OneShotCallTest, Fail_ConnectionClosed) {
  SetUpChannelProviderServer(nullptr);

  const auto result = Run(OneShotCall<fuchsia::update::channelcontrol::ChannelControl,
                                      &fuchsia::update::channelcontrol::ChannelControl::GetCurrent>(
      dispatcher(), services(), kTimeout));
  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), Error::kConnectionError);
}

TEST_F(OneShotCallTest, Fail_Timeout) {
  SetUpChannelProviderServer(std::make_unique<stubs::ChannelControlNeverReturns>());

  const auto result = Run(OneShotCall<fuchsia::update::channelcontrol::ChannelControl,
                                      &fuchsia::update::channelcontrol::ChannelControl::GetCurrent>(
      dispatcher(), services(), kTimeout));

  ASSERT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), Error::kTimeout);
}

}  // namespace
}  // namespace forensics
