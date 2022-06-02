// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/fidl/fidl_synthetic_clock.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-loop/testing/cpp/real_loop.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

namespace media_audio_mixer_service {
namespace {

zx::clock CreateArbitraryZxClock() {
  zx::clock zx_clock;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, &zx_clock);
  FX_CHECK(status == ZX_OK) << "zx::clock::create failed with status " << status;
  return zx_clock;
}

class FidlSyntheticClockTest : public ::testing::Test {
 public:
  void SetUp() {
    loop_.StartThread("fidl_thread");
    auto [client, server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClockRealm>();
    realm_ = FidlSyntheticClockRealm::Create(loop_.dispatcher(), std::move(server));
    realm_client_ = std::move(client);
  }

  void TearDown() {
    // Close the client and wait until the server shuts down.
    realm_client_ = fidl::WireSyncClient<fuchsia_audio_mixer::SyntheticClockRealm>();
    ASSERT_TRUE(realm_->WaitForShutdown(zx::sec(5)));
  }

  template <typename Protocol>
  static std::pair<fidl::WireSyncClient<Protocol>, fidl::ServerEnd<Protocol>> CreateClientOrDie() {
    auto endpoints = fidl::CreateEndpoints<Protocol>();
    if (!endpoints.is_ok()) {
      FX_PLOGS(FATAL, endpoints.status_value()) << "fidl::CreateEndpoints failed";
    }
    return std::make_pair(fidl::BindSyncClient(std::move(endpoints->client)),
                          std::move(endpoints->server));
  }

  static bool IsConnectionAlive(
      const fidl::WireSyncClient<fuchsia_audio_mixer::SyntheticClock>& client) {
    fidl::Arena<> arena;
    return client->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena).Build())
        .ok();
  }

 protected:
  fidl::Arena<> arena_;
  std::shared_ptr<FidlSyntheticClockRealm> realm_;
  fidl::WireSyncClient<fuchsia_audio_mixer::SyntheticClockRealm> realm_client_;

 private:
  async::Loop loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
};

TEST_F(FidlSyntheticClockTest, CreateClockZxClockIsNotReadable) {
  auto result = realm_client_->CreateClock(
      fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("clock"))
          .domain(Clock::kExternalDomain)
          .adjustable(true)
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  ASSERT_TRUE(result.Unwrap_NEW()->value()->has_handle());

  // The clock must be unreadable and unwritable.
  auto zx_clock = std::move(result.Unwrap_NEW()->value()->handle());
  zx_info_handle_basic_t info;
  auto status = zx_clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);
}

TEST_F(FidlSyntheticClockTest, CreateClockWithControl) {
  auto [clock_client, clock_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();
  zx::clock zx_clock;

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
    zx_clock = std::move(result.Unwrap_NEW()->value()->handle());
  }

  // Since the clock is monotonic, it should report the same time as the realm.
  const zx::time clock_t0(
      clock_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t0(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  EXPECT_EQ(clock_t0, realm_t0);

  // Set the clock rate to 1.001x and advance the realm by 100ms.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1000)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }
  {
    auto result = realm_client_->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // The clock should have advanced by 100ms * 1.001 = 100100us.
  const zx::time clock_t1(
      clock_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t1(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());

  EXPECT_EQ(clock_t1, clock_t0 + zx::usec(100100));
  EXPECT_EQ(realm_t1, realm_t0 + zx::msec(100));

  // A second observer should see the same time.
  auto [observe_client, observe_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  const zx::time observe_t1(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());

  EXPECT_EQ(observe_t1, clock_t1);
}

TEST_F(FidlSyntheticClockTest, CreateClockWithoutControl) {
  zx::clock zx_clock;

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
    zx_clock = std::move(result.Unwrap_NEW()->value()->handle());
  }

  // Get an observer.
  auto [observe_client, observe_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Since the clock is monotonic, it should report the same time as the realm.
  const zx::time observe_t0(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t0(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  EXPECT_EQ(observe_t0, realm_t0);

  // Advance the realm by 100ms.
  {
    auto result = realm_client_->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // The clock should have advanced by 100ms.
  const zx::time observe_t1(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t1(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());

  EXPECT_EQ(observe_t1, observe_t0 + zx::msec(100));
  EXPECT_EQ(realm_t1, realm_t0 + zx::msec(100));
}

TEST_F(FidlSyntheticClockTest, CreateGraphControlled) {
  auto zx_clock = realm_->CreateGraphControlled();

  // Get an observer.
  auto [observe_client, observe_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Since the clock is monotonic, it should report the same time as the realm.
  const zx::time observe_t0(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t0(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  EXPECT_EQ(observe_t0, realm_t0);

  // Advance the realm by 100ms.
  {
    auto result = realm_client_->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // The clock should have advanced by 100ms.
  const zx::time observe_t1(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());
  const zx::time realm_t1(
      realm_client_
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          .Unwrap_NEW()
          ->now());

  EXPECT_EQ(observe_t1, observe_t0 + zx::msec(100));
  EXPECT_EQ(realm_t1, realm_t0 + zx::msec(100));
}

TEST_F(FidlSyntheticClockTest, ForgetClosesChannels) {
  auto [clock_client, clock_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();
  zx::clock zx_clock;

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
    zx_clock = std::move(result.Unwrap_NEW()->value()->handle());
  }

  // Connect an observer in additional to the control.
  auto [observe_client, observe_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    zx::clock zx_clock_dup;
    ASSERT_EQ(zx_clock.duplicate(ZX_RIGHT_SAME_RIGHTS, &zx_clock_dup), ZX_OK);

    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock_dup))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Forgetting the clock should drop both connections.
  {
    auto result = realm_client_->ForgetClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Connections are dropped asynchronously, so to avoid test flakes we must poll until the
  // connections are dropped.
  struct Loop : public loop_fixture::RealLoop {
    using RealLoop::RunLoopUntil;
  };
  Loop loop;
  loop.RunLoopUntil([&client = clock_client]() { return !IsConnectionAlive(client); }, zx::sec(5));
  loop.RunLoopUntil([&client = observe_client]() { return !IsConnectionAlive(client); },
                    zx::sec(5));

  EXPECT_FALSE(IsConnectionAlive(clock_client));
  EXPECT_FALSE(IsConnectionAlive(observe_client));
}

TEST_F(FidlSyntheticClockTest, Find) {
  zx::clock zx_clock1 = realm_->CreateGraphControlled();
  zx::clock zx_clock2;

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
    zx_clock2 = std::move(result.Unwrap_NEW()->value()->handle());
  }

  // Both clocks should be found and they should be different clocks.
  const auto koid1 = ClockRegistry::ZxClockToKoid(zx_clock1).value();
  const auto koid2 = ClockRegistry::ZxClockToKoid(zx_clock2).value();

  auto clock1 = realm_->FindOrCreate(std::move(zx_clock1), "unused", 42);
  ASSERT_NE(clock1.get(), nullptr);
  EXPECT_EQ(clock1->name(), "GraphControlled0");
  EXPECT_EQ(clock1->domain(), Clock::kExternalDomain);
  EXPECT_EQ(clock1->adjustable(), true);
  EXPECT_EQ(clock1->koid(), koid1);

  auto clock2 = realm_->FindOrCreate(std::move(zx_clock2), "unused", 42);
  ASSERT_NE(clock2.get(), nullptr);
  EXPECT_EQ(clock2->name(), "clock");
  EXPECT_EQ(clock2->domain(), Clock::kExternalDomain);
  EXPECT_EQ(clock2->adjustable(), false);
  EXPECT_EQ(clock2->koid(), koid2);
}

TEST_F(FidlSyntheticClockTest, SetRateFailsOnUnadjustableClock) {
  auto [clock_client, clock_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(false)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Fail because the clock is not adjustable.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1000)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_ACCESS_DENIED);
  }
}

TEST_F(FidlSyntheticClockTest, SetRateFailsOnAdjustableClock) {
  auto [clock_client, clock_server] = CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result.Unwrap_NEW()->is_error()) << result.Unwrap_NEW()->error_value();
  }

  // Fail because we didn't set the rate parameter.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_).Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because rate > 1000 ppm.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1001)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because rate < -1000 ppm.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(-1001)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(FidlSyntheticClockTest, CreateClockFails) {
  using fuchsia_audio_mixer::CreateClockError;

  // Fail because `domain` is missing.
  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), CreateClockError::kMissingField);
  }

  // Fail because `adjustable` is missing.
  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), CreateClockError::kMissingField);
  }

  // Fail because kMonotonicDomain is not adjustable.
  {
    auto result = realm_client_->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kMonotonicDomain)
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(),
              CreateClockError::kMonotonicDomainIsNotAdjustable);
  }
}

TEST_F(FidlSyntheticClockTest, ForgetClockFails) {
  // Fail because `handle` is missing.
  {
    auto result = realm_client_->ForgetClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockRequest::Builder(arena_).Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `handle` is unknown.
  {
    auto result = realm_client_->ForgetClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(FidlSyntheticClockTest, ObserveClockFails) {
  // Fail because `handle` is missing.
  {
    auto [observe_client, observe_server] =
        CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `observe` is missing.
  {
    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `handle` is unknown.
  {
    auto [observe_client, observe_server] =
        CreateClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

    auto result = realm_client_->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(FidlSyntheticClockTest, AdvanceByFails) {
  // Fails because the duration is negative.
  {
    auto result = realm_client_->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(-1)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fails because the duration is zero.
  {
    auto result = realm_client_->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(0)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result.Unwrap_NEW()->is_error());
    EXPECT_EQ(result.Unwrap_NEW()->error_value(), ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace
}  // namespace media_audio_mixer_service
