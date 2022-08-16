// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/helper.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace input::test {

TEST(InputHelperTest, EventWithReceiverFromViewportTransform) {
  view_tree::Snapshot snapshot;

  const zx_koid_t kRoot = 1;
  const zx_koid_t kChild = 2;
  const zx_koid_t kGrandchild = 3;

  // Create topology:
  //
  //      root
  //        |
  //      child
  //        |
  //   grandchild
  {
    view_tree::ViewNode root_view;
    root_view.children = {kChild};
    root_view.local_from_world_transform = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 5, 5, 5, 1};
    snapshot.view_tree.emplace(kRoot, root_view);
  }

  {
    view_tree::ViewNode child_view;
    child_view.parent = kRoot;
    child_view.children = {kGrandchild};
    child_view.local_from_world_transform = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 10, 10, 10, 1};
    snapshot.view_tree.emplace(kChild, child_view);
  }

  {
    view_tree::ViewNode grandchild_view;
    grandchild_view.parent = kChild;
    grandchild_view.local_from_world_transform = {5, 0, 0, 0, 0,  5,  0,  0,
                                                  0, 0, 5, 0, 15, 15, 15, 1};
    snapshot.view_tree.emplace(kGrandchild, grandchild_view);
  }

  scenic_impl::input::InternalTouchEvent event;
  event.context = kRoot;
  event.target = kChild;
  event.viewport.context_from_viewport_transform = {3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 1};

  auto transform =
      scenic_impl::input::GetDestinationFromViewportTransform(event, kGrandchild, snapshot);

  // Expect the final transform to be:
  //
  // (grandchild's local-from-world) x
  // (inverse of root's local-from-world) x
  // (context-from-viewport)
  EXPECT_EQ(transform[0], 15.f);
  EXPECT_EQ(transform[4], 15.f);
  EXPECT_EQ(transform[6], -10.f);
  EXPECT_EQ(transform[7], -10.f);
}

}  // namespace input::test
