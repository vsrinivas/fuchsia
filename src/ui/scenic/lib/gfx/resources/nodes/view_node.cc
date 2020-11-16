// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"

#include "src/ui/lib/escher/geometry/intersection.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ViewNode::kTypeInfo = {ResourceType::kNode | ResourceType::kViewNode,
                                              "ViewNode"};

ViewNode::ViewNode(Session* session, SessionId session_id, fxl::WeakPtr<View> view)
    : Node(session, session_id, /* node_id */ 0, ViewNode::kTypeInfo), view_(std::move(view)) {}

ViewPtr ViewNode::FindOwningView() const { return ViewPtr(view_.get()); }

// Test the ray against the bounding box of the view, which is
// stored in the properties of the view holder.
Node::IntersectionInfo ViewNode::GetIntersection(
    const escher::ray4& ray, const IntersectionInfo& parent_intersection) const {
  FX_DCHECK(parent_intersection.continue_with_children);

  // Should never register a hit. Views have geometry but are invisible.
  // Ignoring distance, since no hit means no sensible distance value.
  IntersectionInfo result{
      .did_hit = false, .continue_with_children = false, .interval = escher::Interval()};

  const escher::BoundingBox bbox = GetBoundingBox();
  if (!bbox.is_empty()) {
    // Intersect the ray with the view's bounding box.
    escher::Interval hit_interval;
    if (IntersectRayBox(ray, bbox, &hit_interval)) {
      result.interval = parent_intersection.interval.Intersect(hit_interval);
      // Traversal should only continue if the current bounding box is hit and if the
      // interval is non-empty.
      result.continue_with_children = !result.interval.is_empty();
    }
  }

  return result;
}

escher::BoundingBox ViewNode::GetBoundingBox() const {
  View* view = GetView();
  if (!view || !view->view_holder()) {
    return {};
  }

  return view->view_holder()->GetLocalBoundingBox();
}

}  // namespace gfx
}  // namespace scenic_impl
