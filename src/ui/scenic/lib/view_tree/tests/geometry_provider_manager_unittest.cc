// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/geometry_provider_manager.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"
#include "src/ui/scenic/lib/view_tree/tests/utils.h"

namespace view_tree {
namespace geometry_provider_manager::test {
using fuog_ProviderPtr = fuchsia::ui::observation::geometry::ProviderPtr;
using fuog_ProviderWatchResponse = fuchsia::ui::observation::geometry::ProviderWatchResponse;
const auto fuog_BUFFER_SIZE = fuchsia::ui::observation::geometry::BUFFER_SIZE;
const auto fuog_MAX_VIEW_COUNT = fuchsia::ui::observation::geometry::MAX_VIEW_COUNT;

// Generates |num_snapshots| snapshots with |total_nodes| view nodes and triggers the geometry
// provider manager to add the newly generated snapshots to all the registered endpoints.
void PopulateEndpointsWithSnapshots(GeometryProviderManager& geometry_provider_manager,
                                    uint32_t num_snapshots, uint64_t total_nodes) {
  for (uint32_t i = 0; i < num_snapshots; i++) {
    geometry_provider_manager.OnNewViewTreeSnapshot(SingleDepthViewTreeSnapshot(total_nodes));
  }
}

// Unit tests for testing the fuchsia.ui.observation.geometry.Provider
// protocol.
// Class fixture for TEST_F.
class GeometryProviderManagerTest : public gtest::TestLoopFixture {
 protected:
  GeometryProviderManagerTest() {
    geometry_provider_manager_.Register(client_.NewRequest(), kNodeA);

    FX_CHECK(client_.is_bound());
  }

  GeometryProviderManager geometry_provider_manager_;
  fuog_ProviderPtr client_;
};

// Clients waiting for a snapshot get a response as soon as a new snapshot is generated.
TEST_F(GeometryProviderManagerTest, SingleWatchBeforeUpdate) {
  std::optional<fuog_ProviderWatchResponse> client_result;
  const uint32_t num_snapshots = 1;
  const uint64_t num_nodes = 1;

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  // Clients should not receive any snapshots when no snapshots have been generated.
  EXPECT_FALSE(client_result.has_value());

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);
  RunLoopUntilIdle();

  // Clients are sent the new snapshot as soon as a new snapshot is generated.
  ASSERT_TRUE(client_result.has_value());
  EXPECT_EQ(client_result->updates().size(), 1UL);
}

// A Watch call should fail when there is another hanging Watch call by the same client.
TEST_F(GeometryProviderManagerTest, WatchDuringHangingWatch_ShouldFail) {
  fuog_ProviderWatchResponse client_result;
  fuog_ProviderWatchResponse client_result_1;

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  client_->Watch([&client_result_1](auto response) { client_result_1 = std::move(response); });

  RunLoopUntilIdle();

  // Client connection is closed since it tried to make another Watch() call when a Watch() call was
  // already in progress.
  EXPECT_FALSE(client_.is_bound());
}

// Clients receive snapshots when there are snapshots queued up from the time the client had
// registered.
TEST_F(GeometryProviderManagerTest, ClientReceivesPendingSnapshots) {
  std::optional<fuog_ProviderWatchResponse> client_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  // Client should receive all queued up snapshots.
  ASSERT_TRUE(client_result.has_value());
  EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
}

// Client is able to make a successful Watch() call after the previous Watch() call finished
// processing.
TEST_F(GeometryProviderManagerTest, WatchAfterProcessedWatch) {
  {
    std::optional<fuog_ProviderWatchResponse> client_result;
    const uint32_t num_snapshots = fuog_BUFFER_SIZE;
    const uint64_t num_nodes = 1;

    PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());
    ASSERT_TRUE(client_result.has_value());
    EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
  }
  {
    std::optional<fuog_ProviderWatchResponse> client_result;
    const uint32_t num_snapshots = 1;
    const uint64_t num_nodes = 1;

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());
    // Client waits for new snapshots to consume since there are no new snapshots generated after
    // the previous Watch() call.
    ASSERT_FALSE(client_result.has_value());

    PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);
    RunLoopUntilIdle();

    // Client receives the latest generated snapshot.
    ASSERT_TRUE(client_result.has_value());
    EXPECT_EQ(client_result->updates().size(), 1UL);
  }
}

// In case the number of snapshots queued up before the next Watch() call is greater than
// fuchsia::ui::observation::geometry:BUFFER_SIZE, only the latest f.u.o.g.BUFFER_SIZE snapshots are
// returned and the old snapshots are discarded.
TEST_F(GeometryProviderManagerTest, BufferOverflowTest) {
  std::optional<fuog_ProviderWatchResponse> client_result;
  const uint32_t num_snapshots = 2 * fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());
  ASSERT_TRUE(client_result.has_value());

  // Client should receive the latest BUFFER_SIZE snapshot updates.
  // TODO(fxbug.dev/87579): Update the unittest once the implementation of
  // ExtractObservationSnapshot is completed to reflect only the latest snapshot are stored.
  EXPECT_FALSE(client_result->is_complete());
  EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
}

// Clients registered with the protocol should be receiving updates even if one of the clients is
// killed for making an illegal Watch() call.
TEST_F(GeometryProviderManagerTest, MisbehavingClientsShouldNotAffectOtherClients) {
  fuog_ProviderPtr client1;
  fuog_ProviderPtr client2;
  std::optional<fuog_ProviderWatchResponse> client_result;
  std::optional<fuog_ProviderWatchResponse> client1_result;
  std::optional<fuog_ProviderWatchResponse> client2_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  geometry_provider_manager_.Register(client1.NewRequest(), kNodeA);
  geometry_provider_manager_.Register(client2.NewRequest(), kNodeA);

  // Client makes an illegal Watch() call resulting in it being killed.
  client1->Watch([&client1_result](auto response) { client1_result = std::move(response); });
  client1->Watch([&client1_result](auto response) { client1_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_FALSE(client1.is_bound());

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  client2->Watch([&client2_result](auto response) { client2_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());
  EXPECT_TRUE(client2.is_bound());

  // Other clients should still receive pending snapshot updates despite client2 getting killed.
  ASSERT_TRUE(client_result.has_value());
  ASSERT_TRUE(client2_result.has_value());
  EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
  EXPECT_EQ(client2_result->updates().size(), fuog_BUFFER_SIZE);
}

// Other clients should still receive pending snapshot updates even if any other client dies.
TEST_F(GeometryProviderManagerTest, ClientFailuresShouldNotAffectOtherClients) {
  fuog_ProviderPtr client1;
  fuog_ProviderPtr client2;
  std::optional<fuog_ProviderWatchResponse> client_result;
  std::optional<fuog_ProviderWatchResponse> client1_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  geometry_provider_manager_.Register(client1.NewRequest(), kNodeA);
  geometry_provider_manager_.Register(client2.NewRequest(), kNodeA);

  // client2 closes the channel to mock client death.
  client2.Unbind();

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  client1->Watch([&client1_result](auto response) { client1_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());
  EXPECT_TRUE(client1.is_bound());
  EXPECT_FALSE(client2.is_bound());

  // Other clients should still receive pending snapshot updates despite client2 dying.
  ASSERT_TRUE(client_result.has_value());
  ASSERT_TRUE(client1_result.has_value());
  EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
  EXPECT_EQ(client1_result->updates().size(), fuog_BUFFER_SIZE);
}

TEST_F(GeometryProviderManagerTest, ClientDoesNotReceiveViews_WhenViewsCountExceedMaxViewAllowed) {
  std::optional<fuog_ProviderWatchResponse> client_result;
  const uint32_t num_snapshots = 1;
  const uint64_t num_nodes = fuog_MAX_VIEW_COUNT * 2;

  PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  ASSERT_TRUE(client_result.has_value());
  ASSERT_EQ(client_result->updates().size(), 1UL);

  // The client will not receive a views vector in the response as the size of the views vector
  // would have exceeded fuog_MAX_VIEWS. |incomplete| is set to true to notify the client about the
  // same.
  EXPECT_FALSE(client_result->updates()[0].has_views());
  EXPECT_TRUE(client_result->updates()[0].has_incomplete());
  EXPECT_TRUE(client_result->updates()[0].incomplete());
}

// A Watch() call should succeed when size of the response exceeds the maximum size of a message
// that can be sent over the FIDL channel.
TEST_F(GeometryProviderManagerTest, WatchShouldSucceed_WhenResponseSizeExceedsFIDLChannelMaxSize) {
  // The total number of f.u.o.g.ViewTreeSnapshots will always be less than fuog_BUFFER_SIZE when
  // the response size exceeds FIDL channel's limit.
  {
    std::optional<fuog_ProviderWatchResponse> client_result;
    const uint32_t num_snapshots = fuog_BUFFER_SIZE;
    const uint64_t num_nodes = 10;

    PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    ASSERT_TRUE(client_result.has_value());

    EXPECT_FALSE(client_result->is_complete());
    EXPECT_LT(client_result->updates().size(), fuog_BUFFER_SIZE);
  }
  // The response should contain f.u.o.g.ViewTreeSnapshot generated from the most recent snapshot
  // when the response size exceeds the FIDL channel's limit.
  {
    std::optional<fuog_ProviderWatchResponse> client_result;

    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT;
      PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);
    }
    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT - 10;
      PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);
    }
    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT - 100;
      PopulateEndpointsWithSnapshots(geometry_provider_manager_, num_snapshots, num_nodes);
    }

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    // As the number of view nodes in the view tree of the 3 snapshots are large, including
    // f.u.o.g.ViewTreeSnapshots generated from more than 1 snapshot in the response will exceed
    // FIDL channel's limit. Therefore, the server only sends the f.u.o.g.ViewTreeSnapshot generated
    // from the latest snapshot to the client.
    ASSERT_TRUE(client_result.has_value());

    EXPECT_FALSE(client_result->is_complete());
    EXPECT_EQ(client_result->updates().size(), 1UL);
    EXPECT_EQ(client_result->updates()[0].views().size(), fuog_MAX_VIEW_COUNT - 100);
  }
}

}  // namespace geometry_provider_manager::test
}  // namespace view_tree
