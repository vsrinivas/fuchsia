// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"

#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/lib/backoff/backoff.h"

namespace forensics::feedback {
namespace {

using ::testing::IsEmpty;
using ::testing::Pair;
using ::testing::UnorderedElementsAreArray;

constexpr char kChannelKey[] = "current_channel";
constexpr char kChannelValue[] = "channel";

class MonotonicBackoff : public backoff::Backoff {
 public:
  zx::duration GetNext() override {
    const auto backoff = backoff_;
    backoff_ = backoff + zx::sec(1);
    return backoff;
  }
  void Reset() override { backoff_ = zx::sec(1); }

 private:
  zx::duration backoff_{zx::sec(1)};
};

using StaticSingleFidlMethodAnnotationProviderTest = UnitTestFixture;

struct ConvertChannel {
  Annotations operator()(const std::string& channel) { return {{kChannelKey, channel}}; }
};

class CurrentChannelProvider
    : public StaticSingleFidlMethodAnnotationProvider<
          fuchsia::update::channelcontrol::ChannelControl,
          &fuchsia::update::channelcontrol::ChannelControl::GetCurrent, ConvertChannel> {
 public:
  using StaticSingleFidlMethodAnnotationProvider::StaticSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override { return {kChannelKey}; }
};

TEST_F(StaticSingleFidlMethodAnnotationProviderTest, GetAll) {
  CurrentChannelProvider provider(dispatcher(), services(), std::make_unique<MonotonicBackoff>());

  auto channel_server = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannelValue,
  });
  InjectServiceProvider(channel_server.get());

  RunLoopUntilIdle();

  Annotations annotations;
  provider.GetOnce([&annotations](Annotations a) { annotations = std::move(a); });

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({Pair(kChannelKey, kChannelValue)}));
  EXPECT_EQ(channel_server->NumConnections(), 0u);
}

TEST_F(StaticSingleFidlMethodAnnotationProviderTest, Reconnects) {
  CurrentChannelProvider provider(dispatcher(), services(), std::make_unique<MonotonicBackoff>());

  auto channel_server = std::make_unique<stubs::ChannelControlClosesFirstConnection>(
      stubs::ChannelControlBase::Params{
          .current = kChannelValue,
      });
  InjectServiceProvider(channel_server.get());

  RunLoopUntilIdle();
  ASSERT_EQ(channel_server->NumConnections(), 0u);

  Annotations annotations;
  provider.GetOnce([&annotations](Annotations a) { annotations = std::move(a); });

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, IsEmpty());

  RunLoopFor(zx::sec(1));
  EXPECT_THAT(annotations, UnorderedElementsAreArray({Pair(kChannelKey, kChannelValue)}));
  EXPECT_EQ(channel_server->NumConnections(), 0u);
}

}  // namespace
}  // namespace forensics::feedback
