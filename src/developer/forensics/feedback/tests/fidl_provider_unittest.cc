// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"

#include <fuchsia/feedback/cpp/fidl.h>
#include <fuchsia/update/channelcontrol/cpp/fidl.h>
#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/developer/forensics/feedback/annotations/fidl_provider.h"
#include "src/developer/forensics/feedback/annotations/types.h"
#include "src/developer/forensics/testing/stubs/channel_control.h"
#include "src/developer/forensics/testing/stubs/device_id_provider.h"
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
  Annotations operator()(const ErrorOr<std::string>& channel) { return {{kChannelKey, channel}}; }
};

class StaticCurrentChannelProvider
    : public StaticSingleFidlMethodAnnotationProvider<
          fuchsia::update::channelcontrol::ChannelControl,
          &fuchsia::update::channelcontrol::ChannelControl::GetCurrent, ConvertChannel> {
 public:
  using StaticSingleFidlMethodAnnotationProvider::StaticSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override { return {kChannelKey}; }
};

TEST_F(StaticSingleFidlMethodAnnotationProviderTest, GetAll) {
  StaticCurrentChannelProvider provider(dispatcher(), services(),
                                        std::make_unique<MonotonicBackoff>());

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
  StaticCurrentChannelProvider provider(dispatcher(), services(),
                                        std::make_unique<MonotonicBackoff>());

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

using DynamicSingleFidlMethodAnnotationProviderTest = UnitTestFixture;

class DynamicCurrentChannelProvider
    : public DynamicSingleFidlMethodAnnotationProvider<
          fuchsia::update::channelcontrol::ChannelControl,
          &fuchsia::update::channelcontrol::ChannelControl::GetCurrent, ConvertChannel> {
 public:
  using DynamicSingleFidlMethodAnnotationProvider::DynamicSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override { return {kChannelKey}; }
};

TEST_F(DynamicSingleFidlMethodAnnotationProviderTest, Get) {
  DynamicCurrentChannelProvider provider(dispatcher(), services(),
                                         std::make_unique<MonotonicBackoff>());

  auto channel_server = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannelValue,
  });
  InjectServiceProvider(channel_server.get());

  RunLoopUntilIdle();

  Annotations annotations;
  provider.Get([&annotations](Annotations a) { annotations = std::move(a); });

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({Pair(kChannelKey, kChannelValue)}));
  EXPECT_EQ(channel_server->NumConnections(), 1u);
}

TEST_F(DynamicSingleFidlMethodAnnotationProviderTest, Reconnects) {
  DynamicCurrentChannelProvider provider(dispatcher(), services(),
                                         std::make_unique<MonotonicBackoff>());

  auto channel_server = std::make_unique<stubs::ChannelControl>(stubs::ChannelControlBase::Params{
      .current = kChannelValue,
  });
  InjectServiceProvider(channel_server.get());

  RunLoopUntilIdle();
  ASSERT_EQ(channel_server->NumConnections(), 1u);

  channel_server->CloseAllConnections();

  RunLoopUntilIdle();
  ASSERT_EQ(channel_server->NumConnections(), 0u);

  Annotations annotations;
  provider.Get([&annotations](Annotations a) { annotations = std::move(a); });

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({Pair(kChannelKey, Error::kConnectionError)}));

  RunLoopFor(zx::sec(1));
  EXPECT_EQ(channel_server->NumConnections(), 1u);

  provider.Get([&annotations](Annotations a) { annotations = std::move(a); });

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({Pair(kChannelKey, kChannelValue)}));

  channel_server->CloseAllConnections();

  RunLoopUntilIdle();
  EXPECT_EQ(channel_server->NumConnections(), 0u);
}

constexpr char kDeviceIdKey[] = "current_device_id";
constexpr std::array<const char*, 2> kDeviceIdValues = {
    "device_id_1",
    "device_id_2",
};

struct ConvertDeviceId {
  Annotations operator()(const ErrorOr<std::string>& device_id) {
    return {{kDeviceIdKey, device_id}};
  }
};

class HangingGetDeviceIdProvider
    : public HangingGetSingleFidlMethodAnnotationProvider<
          fuchsia::feedback::DeviceIdProvider, &fuchsia::feedback::DeviceIdProvider::GetId,
          ConvertDeviceId> {
 public:
  using HangingGetSingleFidlMethodAnnotationProvider::HangingGetSingleFidlMethodAnnotationProvider;

  std::set<std::string> GetKeys() const override { return {kDeviceIdKey}; }
};

class HangingGetSingleFidlMethodAnnotationProviderTest : public UnitTestFixture {
 protected:
  void SetUpDeviceIdProviderServer(
      std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server) {
    device_id_provider_server_ = std::move(device_id_provider_server);
    if (device_id_provider_server_) {
      InjectServiceProvider(device_id_provider_server_.get());
    }
  }

  std::unique_ptr<stubs::DeviceIdProviderBase> device_id_provider_server_;
};

TEST_F(HangingGetSingleFidlMethodAnnotationProviderTest, Get) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProvider>(kDeviceIdValues[0]));
  HangingGetDeviceIdProvider device_id_provider(dispatcher(), services(),
                                                std::make_unique<MonotonicBackoff>());

  Annotations annotations;
  device_id_provider.GetOnUpdate(
      [&annotations](Annotations result) { annotations = std::move(result); });

  // |annotations| should be empty because the call hasn't completed.
  EXPECT_THAT(annotations, IsEmpty());

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kDeviceIdKey, kDeviceIdValues[0]),
                           }));

  device_id_provider_server_->SetDeviceId(kDeviceIdValues[1]);

  // |annotations| should the old value because the change hasn't propagated yet.
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kDeviceIdKey, kDeviceIdValues[0]),
                           }));

  RunLoopUntilIdle();
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kDeviceIdKey, kDeviceIdValues[1]),
                           }));

  device_id_provider_server_->CloseConnection();

  // |annotations| should contain the old value because the disconnection hasn't propagated.
  EXPECT_THAT(annotations, UnorderedElementsAreArray({
                               Pair(kDeviceIdKey, kDeviceIdValues[1]),
                           }));
}

TEST_F(HangingGetSingleFidlMethodAnnotationProviderTest, Reconnects) {
  SetUpDeviceIdProviderServer(std::make_unique<stubs::DeviceIdProviderNeverReturns>());
  HangingGetDeviceIdProvider device_id_provider(dispatcher(), services(),
                                                std::make_unique<MonotonicBackoff>());

  RunLoopUntilIdle();
  ASSERT_TRUE(device_id_provider_server_->IsBound());

  Annotations annotations;
  device_id_provider.GetOnUpdate(
      [&annotations](Annotations result) { annotations = std::move(result); });

  device_id_provider_server_->CloseConnection();
  ASSERT_FALSE(device_id_provider_server_->IsBound());

  RunLoopUntilIdle();

  // The outstanding request should complete with a connection error.
  EXPECT_THAT(annotations, IsEmpty());
  RunLoopFor(zx::sec(1));
  ASSERT_TRUE(device_id_provider_server_->IsBound());
}

}  // namespace
}  // namespace forensics::feedback
