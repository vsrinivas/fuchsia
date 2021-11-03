// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_VIEW_TREE_TESTS_UTILS_H_
#define SRC_UI_SCENIC_LIB_VIEW_TREE_TESTS_UTILS_H_

#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace view_tree {
enum : zx_koid_t { kNodeA = 1, kNodeB, kNodeC, kNodeD };

// Creates a snapshot with the following two-node topology:
//     A
//     |
//     B
std::shared_ptr<view_tree::Snapshot> TwoNodeSnapshot();

// Creates a snapshot with the following three-node topology:
//     A
//     |
//     B
//     |
//     C
std::shared_ptr<const view_tree::Snapshot> ThreeNodeSnapshot();

// Creates a snapshot with the following four-node topology:
//      A
//    /   \
//   B     C
//   |
//   D
std::shared_ptr<const view_tree::Snapshot> FourNodeSnapshot();

}  // namespace view_tree

#endif  // SRC_UI_SCENIC_LIB_VIEW_TREE_TESTS_UTILS_H_
