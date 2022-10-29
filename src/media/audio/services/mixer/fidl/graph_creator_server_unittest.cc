// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/graph_creator_server.h"

#include <fidl/fuchsia.audio.mixer/cpp/natural_ostream.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/services/common/logging.h"
#include "src/media/audio/services/common/testing/test_server_and_sync_client.h"
#include "src/media/audio/services/mixer/fidl/synthetic_clock_server.h"

namespace media_audio {
namespace {

class GraphCreatorServerTest : public ::testing::Test {
 public:
  void SetUp() {
    thread_ = FidlThread::CreateFromNewThread("test_fidl_thread");
    creator_wrapper_ = std::make_unique<TestServerAndWireSyncClient<GraphCreatorServer>>(thread_);
  }

  void TearDown() {
    // Close the client and wait until the server shuts down.
    creator_wrapper_.reset();
  }

  GraphCreatorServer& creator_server() { return creator_wrapper_->server(); }
  fidl::WireSyncClient<fuchsia_audio_mixer::GraphCreator>& creator_client() {
    return creator_wrapper_->client();
  }

 protected:
  fidl::Arena<> arena_;

 private:
  std::shared_ptr<FidlThread> thread_;
  std::unique_ptr<TestServerAndWireSyncClient<GraphCreatorServer>> creator_wrapper_;
};

TEST_F(GraphCreatorServerTest, CreateGraphMissingServerEnd) {
  auto result =
      creator_client()->Create(fuchsia_audio_mixer::wire::GraphCreatorCreateRequest::Builder(arena_)
                                   .name(fidl::StringView::FromExternal("graph"))
                                   .Build());

  ASSERT_TRUE(result.ok()) << result;
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->error_value(), fuchsia_audio_mixer::CreateGraphError::kInvalidGraphChannel);
}

TEST_F(GraphCreatorServerTest, CreateGraphWithRealClocks) {
  auto [graph_client, graph_server] = CreateWireSyncClientOrDie<fuchsia_audio_mixer::Graph>();

  {
    auto result = creator_client()->Create(
        fuchsia_audio_mixer::wire::GraphCreatorCreateRequest::Builder(arena_)
            .graph(std::move(graph_server))
            .name(fidl::StringView::FromExternal("graph"))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // To make sure we're using real clocks, create a graph-controlled clock and verify it advances.
  {
    auto result = graph_client->CreateGraphControlledReferenceClock();
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();

    auto handle = std::move(result->value()->reference_clock());
    ASSERT_TRUE(handle.is_valid());
    ::media::audio::clock::testing::VerifyAdvances(handle);
  }
}

TEST_F(GraphCreatorServerTest, CreateGraphWithSyntheticClocks) {
  auto [graph_client, graph_server] = CreateWireSyncClientOrDie<fuchsia_audio_mixer::Graph>();
  auto [realm_client, realm_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClockRealm>();

  {
    auto result = creator_client()->Create(
        fuchsia_audio_mixer::wire::GraphCreatorCreateRequest::Builder(arena_)
            .graph(std::move(graph_server))
            .name(fidl::StringView::FromExternal("graph"))
            .synthetic_clock_realm(std::move(realm_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  // To make sure we're using synthetic clocks, create a graph-controlled clock and verify it
  // advances with the synthetic realm.
  zx::clock handle;
  zx::eventpair fence;

  {
    auto result = graph_client->CreateGraphControlledReferenceClock();
    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();

    handle = std::move(result->value()->reference_clock());
    fence = std::move(result->value()->release_fence());
    ASSERT_TRUE(handle.is_valid());
    ASSERT_TRUE(fence.is_valid());
  }

  {
    auto result = realm_client->AdvanceBy(
        fuchsia_audio_mixer::wire::SyntheticClockRealmAdvanceByRequest::Builder(arena_)
            .duration(zx::msec(100).to_nsecs())
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  auto [observe_client, observe_server] =
      CreateWireSyncClientOrDie<fuchsia_audio_mixer::SyntheticClock>();

  {
    auto result = realm_client->ObserveClock(
        fuchsia_audio_mixer::wire::SyntheticClockRealmObserveClockRequest::Builder(arena_)
            .handle(std::move(handle))
            .observe(std::move(observe_server))
            .Build());

    ASSERT_TRUE(result.ok()) << result;
    ASSERT_FALSE(result->is_error()) << result->error_value();
  }

  const zx::time observe_now(
      observe_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockNowRequest::Builder(arena_).Build())
          ->now());
  const zx::time realm_now(
      realm_client
          ->Now(fuchsia_audio_mixer::wire::SyntheticClockRealmNowRequest::Builder(arena_).Build())
          ->now());
  EXPECT_EQ(observe_now, realm_now);
}

}  // namespace
}  // namespace media_audio
