// Copyright 2019. The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/nodes/view_node.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "src/ui/lib/escher/geometry/intersection.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ViewNode::kTypeInfo = {ResourceType::kNode | ResourceType::kView,
                                              "ViewNode"};

ViewNode::ViewNode(Session* session, fxl::WeakPtr<View> view)
    : Node(session, /* node_id */ 0, ViewNode::kTypeInfo), view_(std::move(view)) {}

ViewPtr ViewNode::FindOwningView() const { return ViewPtr(view_.get()); }

// Test the ray against the bounding box of the view, which is
// stored in the properties of the view holder.
Node::IntersectionInfo ViewNode::GetIntersection(
    const escher::ray4& ray, const IntersectionInfo& parent_intersection) const {
  FXL_DCHECK(parent_intersection.continue_with_children);
  IntersectionInfo result;
  View* view = GetView();
  if (view) {
    ViewHolder* holder = view->view_holder();
    if (holder) {
      escher::BoundingBox bbox = holder->GetLocalBoundingBox();

      // Intersect the ray with the view's bounding box.
      escher::Interval hit_interval;
      bool hit_result = IntersectRayBox(ray, bbox, &hit_interval);

      escher::Interval result_interval;
      if (hit_result) {
        result_interval = parent_intersection.interval.Intersect(hit_interval);

        // Only hit if the ray intersects the bounding box AND if the intersection
        // interval is not completely clipped by the parent.
        result.did_hit = !result_interval.is_empty();
      }

      // If there's a hit, intersect the parent's interval with the current hit interval,
      // and use that as the new interval going forward. Otherwise just use the existing
      // interval from the parent.
      result.interval = result.did_hit ? result_interval : parent_intersection.interval;

      // Traversal should only continue if the current bounding box is hit and if the
      // interval is non-empty.
      result.continue_with_children = result.did_hit && !result.interval.is_empty();

      // Hit distance is the same as the minimum interval.
      result.distance = result.interval.min();
    }
  }

  return result;
}

}  // namespace gfx
}  // namespace scenic_impl
