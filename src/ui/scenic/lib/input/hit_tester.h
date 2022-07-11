// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HIT_TESTER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HIT_TESTER_H_

#include <lib/syslog/cpp/macros.h>

#include "lib/inspect/cpp/inspect.h"
#include "src/ui/scenic/lib/input/helper.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// Performs hit testing and tracks hit testing inspect data.
class HitTester {
 public:
  explicit HitTester(const std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                     inspect::Node& parent_node);
  ~HitTester() = default;

  // Perform a hit test with |event| in |view_tree| and returns the koids of all hit views, in order
  // from geometrically closest to furthest from the |event|.
  std::vector<zx_koid_t> HitTest(const Viewport& viewport, glm::vec2 position_in_viewport,
                                 zx_koid_t context, zx_koid_t target, bool semantic_hit_test);

  template <typename T>
  std::vector<zx_koid_t> HitTest(const T& event, bool semantic_hit_test) {
    return HitTest(event.viewport, event.position_in_viewport, event.context, event.target,
                   semantic_hit_test);
  }

  // Returns the koid of the top hit, or ZX_KOID_INVALID if there is none.
  template <typename T>
  zx_koid_t TopHitTest(const T& event, bool semantic_hit_test) {
    const auto hits = HitTest(event, semantic_hit_test);
    return hits.empty() ? ZX_KOID_INVALID : hits.front();
  }

 private:
  // Reference to the ViewTreeSnapshot held by InputSystem.
  const std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot_;

  // Inspect data.
  inspect::Node hit_test_stats_node_;
  inspect::UintProperty num_empty_hit_tests_;
  inspect::UintProperty hits_outside_viewport_;
  inspect::UintProperty context_view_missing_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_HIT_TESTER_H_
