// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/synthetic_clock_server.h"

#include <fidl/fuchsia.audio.mixer/cpp/natural_ostream.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/common/testing/test_server_and_sync_client.h"

namespace media_audio {
namespace {

zx::clock CreateArbitraryZxClock() {
  zx::clock zx_clock;
  auto status =
      zx::clock::create(ZX_CLOCK_OPT_AUTO_START | ZX_CLOCK_OPT_MONOTONIC | ZX_CLOCK_OPT_CONTINUOUS,
                        nullptr, &zx_clock);
  FX_CHECK(status == ZX_OK) << "zx::clock::create failed with status " << status;
  return zx_clock;
}

class SyntheticClockServerTest : public ::testing::Test {
 public:
  void SetUp() {
    thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
    realm_wrapper_ =
        std::make_unique<TestServerAndWireSyncClient<SyntheticClockRealmServer>>(thread_);
  }

  void TearDown() {
    // Close the realm connection and wait for the server to shutdown.
    realm_wrapper_.reset();
  }

  SyntheticClockRealmServer& realm_server() { return realm_wrapper_->server(); }
  fidl::WireSyncClient<fuchsia_audio_mixer::SyntheticClockRealm>& realm_client() {
    return realm_wrapper_->client();
  }

  static bool IsConnectionAlive(
      const fidl::WireSyncClient<fuchsia_audio_mixer::SyntheticClock>& client) {
    fidl::Arena<> arena;
    return client->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena).Build())
        .ok();
  }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<FidlThread> thread_;
  std::unique_ptr<TestServerAndWireSyncClient<SyntheticClockRealmServer>> realm_wrapper_;
};

TEST_F(SyntheticClockServerTest, CreateClockZxClockIsNotReadable) {
  auto result = realm_client()->CreateClock(
      fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
          .name(fidl::StringView::FromExternal("clock"))
          .domain(Clock::kExternalDomain)
          .adjustable(true)
          .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_FALSE(result->is_error()) << result->error_value();
  ASSERT_TRUE(result->value()->has_handle());

  // The clock must be unreadable and unwritable.
  auto zx_clock = std::move(result->value()->handle());
  zx_info_handle_basic_t info;
  auto status = zx_clock.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  ASSERT_EQ(status, ZX_OK);
  EXPECT_EQ(info.rights, ZX_RIGHT_DUPLICATE | ZX_RIGHT_TRANSFER);
}

TEST_F(SyntheticClockServerTest, CreateClockWithControl) {
  auto [clock_client, clock_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();
  zx::clock zx_clock;

  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    zx_clock = std::move(result->value()->handle());
  }

  // Since the clock is monotonic, it should report the same time as the realm.
  const zx::time clock_t0(
      clock_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());
  const zx::time realm_t0(
      realm_client()
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          ->now());
  EXPECT_EQ(clock_t0, realm_t0);

  // Set the clock rate to 1.001x and advance the realm by 100ms.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1000)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }
  {
    auto result = realm_client()->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // The clock should have advanced by 100ms * 1.001 = 100100us.
  const zx::time clock_t1(
      clock_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());
  const zx::time realm_t1(
      realm_client()
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          ->now());

  EXPECT_EQ(clock_t1, clock_t0 + zx::usec(100100));
  EXPECT_EQ(realm_t1, realm_t0 + zx::msec(100));

  // A second observer should see the same time.
  auto [observe_client, observe_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client()->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  const zx::time observe_t1(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());

  EXPECT_EQ(observe_t1, clock_t1);
}

TEST_F(SyntheticClockServerTest, CreateClockWithoutControl) {
  zx::clock zx_clock;

  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
    zx_clock = std::move(result->value()->handle());
  }

  // Get an observer.
  auto [observe_client, observe_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client()->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(zx_clock))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // Since the clock is monotonic, it should report the same time as the realm.
  const zx::time observe_t0(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());
  const zx::time realm_t0(
      realm_client()
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          ->now());
  EXPECT_EQ(observe_t0, realm_t0);

  // Advance the realm by 100ms.
  {
    auto result = realm_client()->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // The clock should have advanced by 100ms.
  const zx::time observe_t1(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());
  const zx::time realm_t1(
      realm_client()
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          ->now());

  EXPECT_EQ(observe_t1, observe_t0 + zx::msec(100));
  EXPECT_EQ(realm_t1, realm_t0 + zx::msec(100));
}

TEST_F(SyntheticClockServerTest, SetRateFailsOnUnadjustableClock) {
  auto [clock_client, clock_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(false)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // Fail because the clock is not adjustable.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1000)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_ACCESS_DENIED);
  }
}

TEST_F(SyntheticClockServerTest, SetRateFailsOnAdjustableClock) {
  auto [clock_client, clock_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .adjustable(true)
            .control(std::move(clock_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // Fail because we didn't set the rate parameter.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_).Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because rate > 1000 ppm.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(1001)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because rate < -1000 ppm.
  {
    auto result = clock_client->SetRate(
        fuchsia_audio_mixer::wire::SyntheticClockSetRateRequest::Builder(arena_)
            .rate_adjust_ppm(-1001)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }
}

TEST_F(SyntheticClockServerTest, CreateClockFails) {
  using fuchsia_audio_mixer::CreateClockError;

  // Fail because `domain` is missing.
  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), CreateClockError::kMissingField);
  }

  // Fail because `adjustable` is missing.
  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kExternalDomain)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), CreateClockError::kMissingField);
  }

  // Fail because kMonotonicDomain is not adjustable.
  {
    auto result = realm_client()->CreateClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmCreateClockRequest::Builder(arena_)
            .name(fidl::StringView::FromExternal("clock"))
            .domain(Clock::kMonotonicDomain)
            .adjustable(true)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), CreateClockError::kMonotonicDomainIsNotAdjustable);
  }
}

TEST_F(SyntheticClockServerTest, ForgetClockFails) {
  // Fail because `handle` is missing.
  {
    auto result = realm_client()->ForgetClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockRequest::Builder(arena_).Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `handle` is unknown.
  {
    auto result = realm_client()->ForgetClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmForgetClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(SyntheticClockServerTest, ObserveClockFails) {
  // Fail because `handle` is missing.
  {
    auto [observe_client, observe_server] =
        CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

    auto result = realm_client()->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `observe` is missing.
  {
    auto result = realm_client()->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fail because `handle` is unknown.
  {
    auto [observe_client, observe_server] =
        CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

    auto result = realm_client()->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(CreateArbitraryZxClock())
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_NOT_FOUND);
  }
}

TEST_F(SyntheticClockServerTest, AdvanceByFails) {
  // Fails because the duration is negative.
  {
    auto result = realm_client()->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(-1)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }

  // Fails because the duration is zero.
  {
    auto result = realm_client()->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(0)
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_TRUE(result->is_error());
    EXPECT_EQ(result->error_value(), ZX_ERR_INVALID_ARGS);
  }
}

}  // namespace
}  // namespace media_audio
