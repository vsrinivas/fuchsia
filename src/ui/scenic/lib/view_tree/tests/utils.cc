// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/view_tree/tests/utils.h"

#include <lib/syslog/cpp/macros.h>

namespace view_tree {

std::shared_ptr<view_tree::Snapshot> TwoNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA};

  return snapshot;
}
std::shared_ptr<const view_tree::Snapshot> ThreeNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA, .children = {kNodeC}};
  view_tree[kNodeC] = ViewNode{.parent = kNodeB};

  return snapshot;
}

std::shared_ptr<const view_tree::Snapshot> FourNodeSnapshot() {
  auto snapshot = std::make_shared<view_tree::Snapshot>();

  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {kNodeB, kNodeC}};
  view_tree[kNodeB] = ViewNode{.parent = kNodeA, .children = {kNodeD}};
  view_tree[kNodeC] = ViewNode{.parent = kNodeA};
  view_tree[kNodeD] = ViewNode{.parent = kNodeB};

  return snapshot;
}

std::shared_ptr<const view_tree::Snapshot> SingleDepthViewTreeSnapshot(uint64_t total_nodes) {
  FX_DCHECK(total_nodes > 0) << "precondition";
  auto snapshot = std::make_shared<view_tree::Snapshot>();
  const float width = 1, height = 1;
  snapshot->root = kNodeA;
  auto& view_tree = snapshot->view_tree;
  view_tree[kNodeA] = ViewNode{.parent = ZX_KOID_INVALID, .children = {}};
  view_tree[kNodeA].bounding_box = {.min = {0, 0}, .max = {{width, height}}};
  for (zx_koid_t i = 0; i < total_nodes - 1; i++) {
    // The minimum node_id for a child can be 2 since node_id of the root is 1.
    view_tree[kNodeA].children.insert(i + 2);
    view_tree[i + 2] = ViewNode{.parent = kNodeA};
    view_tree[i + 2].bounding_box = {.min = {0, 0}, .max = {{width, height}}};
  }
  return snapshot;
}

}  // namespace view_tree
