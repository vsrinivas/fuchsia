// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/hit_tester.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <optional>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/utils/math.h"

#include <glm/glm.hpp>

namespace scenic_impl::input {

namespace {

bool IsOutsideViewport(const Viewport& viewport, const glm::vec2& position_in_viewport) {
  FX_DCHECK(!std::isunordered(position_in_viewport.x, viewport.extents.min.x) &&
            !std::isunordered(position_in_viewport.x, viewport.extents.max.x) &&
            !std::isunordered(position_in_viewport.y, viewport.extents.min.y) &&
            !std::isunordered(position_in_viewport.y, viewport.extents.max.y));
  return position_in_viewport.x < viewport.extents.min.x ||
         position_in_viewport.y < viewport.extents.min.y ||
         position_in_viewport.x > viewport.extents.max.x ||
         position_in_viewport.y > viewport.extents.max.y;
}

}  // namespace

HitTester::HitTester(const std::shared_ptr<const view_tree::Snapshot>& view_tree_snapshot,
                     inspect::Node& parent_node)
    : view_tree_snapshot_(view_tree_snapshot),
      hit_test_stats_node_(parent_node.CreateChild("HitTestStats")),
      num_empty_hit_tests_(hit_test_stats_node_.CreateUint("num_empty_hit_tests", 0)),
      hits_outside_viewport_(hit_test_stats_node_.CreateUint("hit_outside_viewport", 0)),
      context_view_missing_(hit_test_stats_node_.CreateUint("context_view_missing", 0)) {}

std::vector<zx_koid_t> HitTester::HitTest(const Viewport& viewport,
                                          const glm::vec2 position_in_viewport,
                                          const zx_koid_t context, const zx_koid_t target,
                                          const bool semantic_hit_test) {
  if (IsOutsideViewport(viewport, position_in_viewport)) {
    hits_outside_viewport_.Add(1);
    return {};
  }

  const std::optional<glm::mat4> world_from_context_transform =
      view_tree_snapshot_->GetWorldFromViewTransform(context);
  if (!world_from_context_transform) {
    num_empty_hit_tests_.Add(1);
    context_view_missing_.Add(1);
    return {};
  }

  const auto world_from_viewport_transform =
      world_from_context_transform.value() * viewport.context_from_viewport_transform;
  const auto world_space_point =
      utils::TransformPointerCoords(position_in_viewport, world_from_viewport_transform);
  auto hits = view_tree_snapshot_->HitTest(target, world_space_point, semantic_hit_test);
  if (hits.empty()) {
    num_empty_hit_tests_.Add(1);
  }
  return hits;
}

}  // namespace scenic_impl::input
