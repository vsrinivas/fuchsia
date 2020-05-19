// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "view_wrapper.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ViewWrapper::ViewWrapper(fuchsia::ui::views::ViewRef view_ref,
                         std::unique_ptr<ViewSemantics> view_semantics,
                         std::unique_ptr<AnnotationViewInterface> annotation_view)
    : view_ref_(std::move(view_ref)),
      view_semantics_(std::move(view_semantics)),
      annotation_view_(std::move(annotation_view)) {}

void ViewWrapper::EnableSemanticUpdates(bool enabled) {
  view_semantics_->EnableSemanticUpdates(enabled);
}

fxl::WeakPtr<::a11y::SemanticTree> ViewWrapper::GetTree() { return view_semantics_->GetTree(); }

fuchsia::ui::views::ViewRef ViewWrapper::ViewRefClone() const { return Clone(view_ref_); }

void ViewWrapper::HighlightNode(uint32_t node_id) {
  auto tree_weak_ptr = GetTree();

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewWrapper::DrawHighlight: Invalid tree pointer";
    return;
  }

  auto annotated_node = tree_weak_ptr->GetNode(node_id);

  if (!annotated_node) {
    FX_LOGS(ERROR) << "ViewWrapper::DrawHighlight: No node found with id: " << node_id;
    return;
  }

  auto bounding_box = annotated_node->location();
  annotation_view_->DrawHighlight(bounding_box);
}

void ViewWrapper::ClearHighlights() { annotation_view_->DetachViewContents(); }

}  // namespace a11y
