// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/geometry_provider.h"

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"
#include "src/ui/scenic/lib/view_tree/tests/utils.h"

namespace view_tree {
namespace geometry_provider::test {
using fuog_ProviderPtr = fuchsia::ui::observation::geometry::ViewTreeWatcherPtr;
using fuog_WatchResponse = fuchsia::ui::observation::geometry::WatchResponse;
using fuc_ViewportProperties = fuchsia::ui::composition::ViewportProperties;
const auto fuog_BUFFER_SIZE = fuchsia::ui::observation::geometry::BUFFER_SIZE;
const auto fuog_MAX_VIEW_COUNT = fuchsia::ui::observation::geometry::MAX_VIEW_COUNT;

// Generates |num_snapshots| snapshots with |total_nodes| view nodes and triggers the geometry
// provider manager to add the newly generated snapshots to all the registered endpoints.
void PopulateEndpointsWithSnapshots(GeometryProvider& geometry_provider, uint32_t num_snapshots,
                                    uint64_t total_nodes,
                                    std::optional<bool> gfx_is_rendering = std::nullopt) {
  for (uint32_t i = 0; i < num_snapshots; i++) {
    geometry_provider.OnNewViewTreeSnapshot(
        SingleDepthViewTreeSnapshot(total_nodes, gfx_is_rendering));
  }
}

// Unit tests for testing the fuchsia.ui.observation.geometry.ViewTreeWatcher
// protocol.
// Class fixture for TEST_F.
class GeometryProviderTest : public gtest::TestLoopFixture {
 protected:
  GeometryProviderTest() {
    geometry_provider_.Register(client_.NewRequest(), kNodeA);

    FX_CHECK(client_.is_bound());
  }

  GeometryProvider geometry_provider_;
  fuog_ProviderPtr client_;
};

// Clients waiting for a snapshot get a response as soon as a new snapshot is generated.
TEST_F(GeometryProviderTest, SingleWatchBeforeUpdate) {
  std::optional<fuog_WatchResponse> client_result;
  const uint32_t num_snapshots = 1;
  const uint64_t num_nodes = 1;

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  // Clients should not receive any snapshots when no snapshots have been generated.
  EXPECT_FALSE(client_result.has_value());

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
  RunLoopUntilIdle();

  // Clients are sent the new snapshot as soon as a new snapshot is generated.
  ASSERT_TRUE(client_result.has_value());
  EXPECT_EQ(client_result->updates().size(), 1UL);
}

// A Watch call should fail when there is another hanging Watch call by the same client.
TEST_F(GeometryProviderTest, WatchDuringHangingWatch_ShouldFail) {
  fuog_WatchResponse client_result;
  fuog_WatchResponse client_result_1;

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  client_->Watch([&client_result_1](auto response) { client_result_1 = std::move(response); });

  RunLoopUntilIdle();

  // Client connection is closed since it tried to make another Watch() call when a
  // Watch() call was already in progress.
  EXPECT_FALSE(client_.is_bound());
}

// Clients receive snapshots when there are snapshots queued up from the time the client had
// registered.
TEST_F(GeometryProviderTest, ClientReceivesPendingSnapshots) {
  std::optional<fuog_WatchResponse> client_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  // Client should receive all queued up snapshots.
  ASSERT_TRUE(client_result.has_value());
  EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
}

// Client is able to make a successful Watch() call after the previous Watch() call
// finished processing.
TEST_F(GeometryProviderTest, WatchAfterProcessedWatch) {
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = fuog_BUFFER_SIZE;
    const uint64_t num_nodes = 1;

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());
    ASSERT_TRUE(client_result.has_value());
    EXPECT_EQ(client_result->updates().size(), fuog_BUFFER_SIZE);
  }
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = 1;
    const uint64_t num_nodes = 1;

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());
    // Client waits for new snapshots to consume since there are no new snapshots generated after
    // the previous Watch() call.
    ASSERT_FALSE(client_result.has_value());

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
    RunLoopUntilIdle();

    // Client receives the latest generated snapshot.
    ASSERT_TRUE(client_result.has_value());
    EXPECT_EQ(client_result->updates().size(), 1UL);
  }
}

// In case the number of snapshots queued up before the next Watch() call is greater than
// fuchsia::ui::observation::geometry:BUFFER_SIZE, only the latest f.u.o.g.BUFFER_SIZE snapshots are
// returned and the old snapshots are discarded.
TEST_F(GeometryProviderTest, BufferOverflowTest) {
  std::optional<fuog_WatchResponse> client_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes + 1);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());
  ASSERT_TRUE(client_result.has_value());

  // Client should receive the latest BUFFER_SIZE snapshot updates. The latest snapshots have
  // |num_nodes|+1 view nodes.
  ASSERT_TRUE(client_result->has_error());
  EXPECT_TRUE(client_result->error() | fuchsia::ui::observation::geometry::Error::BUFFER_OVERFLOW);
  for (auto& snapshot : client_result->updates()) {
    EXPECT_EQ(snapshot.views().size(), num_nodes + 1);
  }
}

// Clients registered with the protocol should be receiving updates even if one of the clients is
// killed for making an illegal Watch() call.
TEST_F(GeometryProviderTest, MisbehavingClientsShouldNotAffectOtherClients) {
  fuog_ProviderPtr client1;
  fuog_ProviderPtr client2;
  std::optional<fuog_WatchResponse> client_result;
  std::optional<fuog_WatchResponse> client1_result;
  std::optional<fuog_WatchResponse> client2_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  geometry_provider_.Register(client1.NewRequest(), kNodeA);
  geometry_provider_.Register(client2.NewRequest(), kNodeA);

  // Client makes an illegal Watch() call resulting in it being killed.
  client1->Watch([&client1_result](auto response) { client1_result = std::move(response); });
  client1->Watch([&client1_result](auto response) { client1_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_FALSE(client1.is_bound());

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

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
TEST_F(GeometryProviderTest, ClientFailuresShouldNotAffectOtherClients) {
  fuog_ProviderPtr client1;
  fuog_ProviderPtr client2;
  std::optional<fuog_WatchResponse> client_result;
  std::optional<fuog_WatchResponse> client1_result;
  const uint32_t num_snapshots = fuog_BUFFER_SIZE;
  const uint64_t num_nodes = 1;

  geometry_provider_.Register(client1.NewRequest(), kNodeA);
  geometry_provider_.Register(client2.NewRequest(), kNodeA);

  // client2 closes the channel to mock client death.
  client2.Unbind();

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

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

TEST_F(GeometryProviderTest, ClientDoesNotReceiveViews_WhenViewsCountExceedMaxViewAllowed) {
  std::optional<fuog_WatchResponse> client_result;
  const uint32_t num_snapshots = 1;
  const uint64_t num_nodes = fuog_MAX_VIEW_COUNT * 2;

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

  client_->Watch([&client_result](auto response) { client_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client_.is_bound());

  ASSERT_TRUE(client_result.has_value());
  ASSERT_EQ(client_result->updates().size(), 1UL);

  // The client will not receive a views vector in the response as the size of the views vector
  // would have exceeded fuog_MAX_VIEWS.
  EXPECT_FALSE(client_result->updates()[0].has_views());
}

// A Watch() call should succeed when size of the response exceeds the maximum size of a
// message that can be sent over the FIDL channel.
TEST_F(GeometryProviderTest, WatchShouldSucceed_WhenResponseSizeExceedsFIDLChannelMaxSize) {
  // The total number of f.u.o.g.ViewTreeSnapshots will always be less than fuog_BUFFER_SIZE when
  // the response size exceeds FIDL channel's limit.
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = fuog_BUFFER_SIZE;
    const uint64_t num_nodes = 10;

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    ASSERT_TRUE(client_result.has_value());

    ASSERT_TRUE(client_result->has_error());
    EXPECT_TRUE(client_result->error() |
                fuchsia::ui::observation::geometry::Error::CHANNEL_OVERFLOW);
    EXPECT_LT(client_result->updates().size(), fuog_BUFFER_SIZE);
  }
  // The response should contain f.u.o.g.ViewTreeSnapshot generated from the most recent snapshot
  // when the response size exceeds the FIDL channel's limit.
  {
    std::optional<fuog_WatchResponse> client_result;

    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT;
      PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
    }
    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT - 10;
      PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
    }
    {
      const uint32_t num_snapshots = 1;
      const uint64_t num_nodes = fuog_MAX_VIEW_COUNT - 100;
      PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);
    }

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    // As the number of view nodes in the view tree of the 3 snapshots are large, including
    // f.u.o.g.ViewTreeSnapshots generated from more than 1 snapshot in the response will exceed
    // FIDL channel's limit. Therefore, the server only sends the f.u.o.g.ViewTreeSnapshot generated
    // from the latest snapshot to the client.
    ASSERT_TRUE(client_result.has_value());

    ASSERT_TRUE(client_result->has_error());
    EXPECT_TRUE(client_result->error() |
                fuchsia::ui::observation::geometry::Error::CHANNEL_OVERFLOW);
    EXPECT_EQ(client_result->updates().size(), 1UL);
    EXPECT_EQ(client_result->updates()[0].views().size(), fuog_MAX_VIEW_COUNT - 100);
  }
}

// fuog_ViewDescriptor should accurately capture data from a view_tree::ViewNode. The test uses
// the following three node topology:
// node_a (root)
//  |
// node_b
//  |
// node_c
TEST_F(GeometryProviderTest, ExtractObservationSnapshotTest) {
  auto snapshot = std::make_shared<view_tree::Snapshot>();
  zx_koid_t node_a_koid = 1, node_b_koid = 2, node_c_koid = 3;
  auto node_a = ViewNode{.children = {node_b_koid}};
  auto node_b = ViewNode{.parent = node_a_koid, .children = {node_c_koid}};
  auto node_c = ViewNode{.parent = node_b_koid};

  // Set up node_a.
  {
    const uint32_t width = 10, height = 10;
    const float pixel_scale_x = 1.f, pixel_scale_y = 1.f;
    const fuchsia::math::InsetF inset = {.top = 1.f, .right = 1.f, .bottom = 1.f, .left = 1.f};
    view_tree::BoundingBox bounding_box = {.min = {0, 0}, .max = {width, height}};
    node_a.bounding_box = std::move(bounding_box);
    node_a.gfx_pixel_scale = {pixel_scale_x, pixel_scale_y};
    node_a.gfx_inset = std::move(inset);
  }

  // Set up node_b.
  {
    const uint32_t width = 5, height = 5;
    const float pixel_scale_x = 2.f, pixel_scale_y = 2.f;
    const fuchsia::math::InsetF inset = {.top = 2.f, .right = 2.f, .bottom = 2.f, .left = 2.f};
    view_tree::BoundingBox bounding_box = {.min = {0, 0}, .max = {width, height}};
    node_b.bounding_box = std::move(bounding_box);
    node_b.gfx_pixel_scale = {pixel_scale_x, pixel_scale_y};
    node_b.gfx_inset = std::move(inset);
  }

  // Set up node_c.
  {
    const uint32_t width = 1, height = 1;
    const float pixel_scale_x = 3.f, pixel_scale_y = 3.f;
    const fuchsia::math::InsetF inset = {.top = 3.f, .right = 3.f, .bottom = 3.f, .left = 3.f};
    view_tree::BoundingBox bounding_box = {.min = {0, 0}, .max = {width, height}};
    node_c.bounding_box = std::move(bounding_box);
    node_c.gfx_pixel_scale = {pixel_scale_x, pixel_scale_y};
    node_c.gfx_inset = std::move(inset);
  }

  // Client should receive an empty views vector in the response when a view_tree::Snapshot has no
  // views.
  {
    auto view_tree_snapshot = view_tree::GeometryProvider::ExtractObservationSnapshot(
        /*context_view*/ std::nullopt, snapshot);
    ASSERT_TRUE(view_tree_snapshot);
    ASSERT_TRUE(view_tree_snapshot->has_views());
    EXPECT_TRUE(view_tree_snapshot->views().empty());
  }

  snapshot->root = node_a_koid;
  snapshot->view_tree.try_emplace(node_a_koid, std::move(node_a));
  snapshot->view_tree.try_emplace(node_b_koid, std::move(node_b));
  snapshot->view_tree.try_emplace(node_c_koid, std::move(node_c));

  // Client should receive fuog_ViewDescriptor for every node in the view tree since the root node
  // is the context view.
  {
    auto view_tree_snapshot = view_tree::GeometryProvider::ExtractObservationSnapshot(
        /*context_view*/ node_a_koid, snapshot);

    ASSERT_TRUE(view_tree_snapshot);
    ASSERT_TRUE(view_tree_snapshot->has_views());
    ASSERT_EQ(view_tree_snapshot->views().size(), 3UL);

    // fuog_ViewDescriptor for node_a.
    {
      auto& vd = view_tree_snapshot->views()[0];

      ASSERT_TRUE(vd.has_view_ref_koid());
      EXPECT_EQ(vd.view_ref_koid(), node_a_koid);

      ASSERT_TRUE(vd.has_layout());
      auto& layout = vd.layout();
      auto node_logical_width =
          static_cast<float>(snapshot->view_tree[node_a_koid].bounding_box.max[0]);
      auto node_logical_height =
          static_cast<float>(snapshot->view_tree[node_a_koid].bounding_box.max[1]);
      auto node_pixel_scale = snapshot->view_tree[node_a_koid].gfx_pixel_scale;
      auto node_inset = snapshot->view_tree[node_a_koid].gfx_inset;

      // Minimum coordinates for a layout should be its origin and maximum coordinates should be
      // equal to the node's logical size.
      EXPECT_FLOAT_EQ(layout.extent.min.x, 0.);
      EXPECT_FLOAT_EQ(layout.extent.min.y, 0.);
      EXPECT_FLOAT_EQ(layout.extent.max.x, node_logical_width);
      EXPECT_FLOAT_EQ(layout.extent.max.y, node_logical_height);
      EXPECT_THAT(layout.pixel_scale, testing::ElementsAreArray(*node_pixel_scale));
      EXPECT_FLOAT_EQ(layout.inset.top, node_inset->top);
      EXPECT_FLOAT_EQ(layout.inset.right, node_inset->right);
      EXPECT_FLOAT_EQ(layout.inset.bottom, node_inset->bottom);
      EXPECT_FLOAT_EQ(layout.inset.left, node_inset->left);

      ASSERT_TRUE(vd.has_extent_in_context());
      auto& extent_in_context = vd.extent_in_context();

      // For the context view, |extent_in_context| should be the same as its |layout|.
      EXPECT_FLOAT_EQ(extent_in_context.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_context.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_context.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_extent_in_parent());
      auto& extent_in_parent = vd.extent_in_parent();

      // For the context view, |extent_in_parent| should be the same as its |layout|.
      EXPECT_FLOAT_EQ(extent_in_parent.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_parent.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_parent.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_children());
      EXPECT_THAT(vd.children(), testing::UnorderedElementsAre(static_cast<uint32_t>(node_b_koid)));
    }

    // fuog_ViewDescriptor for node_b.
    {
      auto& vd = view_tree_snapshot->views()[1];

      ASSERT_TRUE(vd.has_view_ref_koid());
      EXPECT_EQ(vd.view_ref_koid(), node_b_koid);

      ASSERT_TRUE(vd.has_layout());
      auto& layout = vd.layout();
      auto node_logical_width =
          static_cast<float>(snapshot->view_tree[node_b_koid].bounding_box.max[0]);
      auto node_logical_height =
          static_cast<float>(snapshot->view_tree[node_b_koid].bounding_box.max[1]);
      auto node_pixel_scale = snapshot->view_tree[node_b_koid].gfx_pixel_scale;
      auto node_inset = snapshot->view_tree[node_b_koid].gfx_inset;

      EXPECT_FLOAT_EQ(layout.extent.min.x, 0.);
      EXPECT_FLOAT_EQ(layout.extent.min.y, 0.);
      EXPECT_FLOAT_EQ(layout.extent.max.x, node_logical_width);
      EXPECT_FLOAT_EQ(layout.extent.max.y, node_logical_height);
      EXPECT_THAT(layout.pixel_scale, testing::ElementsAreArray(*node_pixel_scale));
      EXPECT_FLOAT_EQ(layout.inset.top, node_inset->top);
      EXPECT_FLOAT_EQ(layout.inset.right, node_inset->right);
      EXPECT_FLOAT_EQ(layout.inset.bottom, node_inset->bottom);
      EXPECT_FLOAT_EQ(layout.inset.left, node_inset->left);

      ASSERT_TRUE(vd.has_extent_in_context());
      auto& extent_in_context = vd.extent_in_context();

      // As all the nodes in the view_tree have |local_from_world_transform| as identity matrix,
      // |extent_in_context| and |extent_in_parent| will be the same as layout.
      EXPECT_FLOAT_EQ(extent_in_context.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_context.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_context.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_extent_in_parent());
      auto& extent_in_parent = vd.extent_in_parent();

      EXPECT_FLOAT_EQ(extent_in_parent.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_parent.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_parent.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_children());
      EXPECT_THAT(vd.children(), testing::UnorderedElementsAre(static_cast<uint32_t>(node_c_koid)));
    }

    // fuog_ViewDescriptor for node_c.
    {
      auto& vd = view_tree_snapshot->views()[2];

      ASSERT_TRUE(vd.has_view_ref_koid());
      EXPECT_EQ(vd.view_ref_koid(), node_c_koid);

      ASSERT_TRUE(vd.has_layout());
      auto& layout = vd.layout();
      auto node_logical_width =
          static_cast<float>(snapshot->view_tree[node_c_koid].bounding_box.max[0]);
      auto node_logical_height =
          static_cast<float>(snapshot->view_tree[node_c_koid].bounding_box.max[1]);
      auto node_pixel_scale = snapshot->view_tree[node_c_koid].gfx_pixel_scale;
      auto node_inset = snapshot->view_tree[node_c_koid].gfx_inset;

      EXPECT_FLOAT_EQ(layout.extent.min.x, 0.);
      EXPECT_FLOAT_EQ(layout.extent.min.y, 0.);
      EXPECT_FLOAT_EQ(layout.extent.max.x, node_logical_width);
      EXPECT_FLOAT_EQ(layout.extent.max.y, node_logical_height);
      EXPECT_THAT(layout.pixel_scale, testing::ElementsAreArray(*node_pixel_scale));
      EXPECT_FLOAT_EQ(layout.inset.top, node_inset->top);
      EXPECT_FLOAT_EQ(layout.inset.right, node_inset->right);
      EXPECT_FLOAT_EQ(layout.inset.bottom, node_inset->bottom);
      EXPECT_FLOAT_EQ(layout.inset.left, node_inset->left);

      ASSERT_TRUE(vd.has_extent_in_context());
      auto& extent_in_context = vd.extent_in_context();

      EXPECT_FLOAT_EQ(extent_in_context.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_context.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_context.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_context.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_extent_in_parent());
      auto& extent_in_parent = vd.extent_in_parent();

      EXPECT_FLOAT_EQ(extent_in_parent.origin.x, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.origin.y, 0.);
      EXPECT_FLOAT_EQ(extent_in_parent.width, node_logical_width);
      EXPECT_FLOAT_EQ(extent_in_parent.height, node_logical_height);
      EXPECT_FLOAT_EQ(extent_in_parent.angle_degrees, 0.);

      ASSERT_TRUE(vd.has_children());
      EXPECT_TRUE(vd.children().empty());
    }
  }

  // Client should receive fuog_ViewDescriptor for the context_view only as the context_view is a
  // leaf node.
  {
    auto view_tree_snapshot = view_tree::GeometryProvider::ExtractObservationSnapshot(
        /*context_view*/ node_c_koid, snapshot);

    ASSERT_TRUE(view_tree_snapshot);
    ASSERT_TRUE(view_tree_snapshot->has_views());
    ASSERT_EQ(view_tree_snapshot->views().size(), 1UL);

    auto& vd = view_tree_snapshot->views()[0];
    ASSERT_TRUE(vd.has_view_ref_koid());
    EXPECT_EQ(vd.view_ref_koid(), node_c_koid);
  }
}

// Clients registered through |RegisterGlobalViewTreeWatcher| should receive information about all
// the nodes in a view tree.
TEST_F(GeometryProviderTest, RegisterGlobalViewTreeWatcherTest) {
  fuog_ProviderPtr client;
  std::optional<fuog_WatchResponse> client_result;
  const uint32_t num_snapshots = 1;
  const uint64_t num_nodes = 5;

  geometry_provider_.RegisterGlobalViewTreeWatcher(client.NewRequest());

  PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

  client->Watch([&client_result](auto response) { client_result = std::move(response); });

  RunLoopUntilIdle();

  ASSERT_TRUE(client_result.has_value());
  ASSERT_TRUE(client_result->has_updates());
  EXPECT_FALSE(client_result->has_error());
  ASSERT_EQ(client_result->updates().size(), num_snapshots);

  // Client should receive fuog_ViewDescriptors for |num_nodes| since it has a unlimited access to
  // the global view tree.
  ASSERT_TRUE(client_result->updates()[0].has_views());
  EXPECT_EQ(client_result->updates()[0].views().size(), num_nodes);
}

// For GFX clients, the response of a Watch() call should only contain fuog_ViewDescriptors
// for the view nodes that have rendered. However, in the case of flatland as views do not generate
// a |is_rendering| signal, the view nodes present in a ViewTreeSnapshot will be present in a
// Watch() call's response.
TEST_F(GeometryProviderTest, GfxIsRenderingTest) {
  // |gfx_is_rendering| is not set which means that the view nodes belong to flatland instances.
  // Hence |num_nodes| fuog_ViewDescriptors are present in the response.
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = 1;
    const uint64_t num_nodes = 1;

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    ASSERT_TRUE(client_result.has_value());

    EXPECT_EQ(client_result->updates().size(), num_snapshots);
    EXPECT_EQ(client_result->updates()[0].views().size(), num_nodes);
  }

  // |gfx_is_rendering| is set as false which means the view nodes belonging to GFX instances have
  // not rendered any content. Hence no fuog_ViewDescriptors are present in the response.
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = 1;
    const uint64_t num_nodes = 1;

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes,
                                   /*gfx_is_rendering*/ false);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    ASSERT_TRUE(client_result.has_value());

    EXPECT_EQ(client_result->updates().size(), num_snapshots);
    EXPECT_TRUE(client_result->updates()[0].views().empty());
  }

  // |gfx_is_rendering| is set as true which means the view nodes belonging to GFX instances have
  // rendered some content. Hence |num_nodes| fuog_ViewDescriptors are present in the response.
  {
    std::optional<fuog_WatchResponse> client_result;
    const uint32_t num_snapshots = 1;
    const uint64_t num_nodes = 1;

    PopulateEndpointsWithSnapshots(geometry_provider_, num_snapshots, num_nodes,
                                   /*gfx_is_rendering*/ true);

    client_->Watch([&client_result](auto response) { client_result = std::move(response); });
    RunLoopUntilIdle();

    EXPECT_TRUE(client_.is_bound());

    ASSERT_TRUE(client_result.has_value());

    EXPECT_EQ(client_result->updates().size(), num_snapshots);
    EXPECT_EQ(client_result->updates()[0].views().size(), num_nodes);
  }
}

// Clients registered using |fuchsia.ui.observation.scope.Registry| get updates about its
// |context_view| and other descendant views.
TEST_F(GeometryProviderTest, ScopedRegistryTest) {
  const zx_koid_t node_a_koid = 1, node_b_koid = 2;
  const float width = 1, height = 1;

  fuog_ProviderPtr client;
  std::optional<fuog_WatchResponse> client_result;
  geometry_provider_.Register(client.NewRequest(), node_b_koid);

  // Generate an empty view tree snapshot.
  {
    auto snapshot = std::make_shared<view_tree::Snapshot>();
    geometry_provider_.OnNewViewTreeSnapshot(snapshot);
  }

  client->Watch([&client_result](auto response) { client_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client.is_bound());

  ASSERT_TRUE(client_result.has_value());

  // Client receives an empty views vector in the response when the view tree is empty.
  EXPECT_EQ(client_result->updates().size(), 1UL);
  EXPECT_TRUE(client_result->updates()[0].views().empty());

  // Generate a snapshot containing only |node_a|.
  {
    auto snapshot = std::make_shared<view_tree::Snapshot>();
    auto node_a = ViewNode{.bounding_box = {.min = {0, 0}, .max = {width, height}}};
    snapshot->root = node_a_koid;
    snapshot->view_tree.try_emplace(node_a_koid, std::move(node_a));
    geometry_provider_.OnNewViewTreeSnapshot(snapshot);
  }

  client->Watch([&client_result](auto response) { client_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client.is_bound());

  ASSERT_TRUE(client_result.has_value());

  // Client receives an empty views vector in the response as its |context_view| is not present in
  // the view tree.
  EXPECT_EQ(client_result->updates().size(), 1UL);
  EXPECT_TRUE(client_result->updates()[0].views().empty());

  // Generate a snapshot with |node_a| as the root and |node_b| as the child of |node_a|.
  {
    auto snapshot = std::make_shared<view_tree::Snapshot>();
    auto node_a = ViewNode{.children = {node_b_koid},
                           .bounding_box = {.min = {0, 0}, .max = {width, height}}};
    auto node_b =
        ViewNode{.parent = node_a_koid, .bounding_box = {.min = {0, 0}, .max = {width, height}}};
    snapshot->root = node_a_koid;
    snapshot->view_tree.try_emplace(node_a_koid, std::move(node_a));
    snapshot->view_tree.try_emplace(node_b_koid, std::move(node_b));
    geometry_provider_.OnNewViewTreeSnapshot(snapshot);
  }

  client->Watch([&client_result](auto response) { client_result = std::move(response); });
  RunLoopUntilIdle();

  EXPECT_TRUE(client.is_bound());

  ASSERT_TRUE(client_result.has_value());

  // Client receives updates about its |context_view| in the response as it is now present in the
  // view tree.
  EXPECT_EQ(client_result->updates().size(), 1UL);
  EXPECT_EQ(client_result->updates()[0].views().size(), 1UL);
}

}  // namespace geometry_provider::test
}  // namespace view_tree
