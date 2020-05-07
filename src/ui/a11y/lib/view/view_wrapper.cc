// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "view_wrapper.h"

#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

ViewWrapper::ViewWrapper(
    fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request,
    sys::ComponentContext* context,
    std::unique_ptr<AnnotationViewFactoryInterface> annotation_view_factory)
    : view_ref_(std::move(view_ref)),
      semantic_tree_binding_(std::move(tree_service_ptr), std::move(semantic_tree_request)),
      annotation_view_factory_(std::move(annotation_view_factory)) {
  // TODO: Remove this condition once view manager is updated.
  if (!annotation_view_factory_) {
    return;
  }

  annotation_view_ = annotation_view_factory_->CreateAndInitAnnotationView(
      ViewRefClone(), context,
      // callback invoked when view properties change
      [this]() {
        if (annotation_state_.has_annotations) {
          DrawHighlight();
        }
      },
      // callback invoked when view is attached to scene
      [this]() {
        if (annotation_state_.has_annotations) {
          DrawHighlight();
        }
      },
      // callback invoked when view is detached from scene
      [this]() {
        if (annotation_state_.has_annotations) {
          HideHighlights();
        }
      });
}

ViewWrapper::~ViewWrapper() { semantic_tree_binding_.Unbind(); }

void ViewWrapper::EnableSemanticUpdates(bool enabled) {
  semantic_tree_binding_.impl()->EnableSemanticsUpdates(enabled);
}

fxl::WeakPtr<::a11y::SemanticTree> ViewWrapper::GetTree() {
  return semantic_tree_binding_.impl()->Get();
}

fuchsia::ui::views::ViewRef ViewWrapper::ViewRefClone() const { return Clone(view_ref_); }

void ViewWrapper::HighlightNode(uint32_t node_id) {
  annotation_state_.has_annotations = true;
  annotation_state_.annotated_node_id = node_id;

  DrawHighlight();
}

void ViewWrapper::ClearHighlights() {
  annotation_state_.has_annotations = false;
  annotation_state_.annotated_node_id = std::nullopt;

  HideHighlights();
}

void ViewWrapper::DrawHighlight() {
  if (!annotation_view_) {
    return;
  }

  if (!annotation_state_.has_annotations || !annotation_state_.annotated_node_id.has_value()) {
    return;
  }

  auto tree_weak_ptr = GetTree();

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewWrapper::DrawHighlight: Invalid tree pointer";
    return;
  }

  auto annotated_node = tree_weak_ptr->GetNode(*(annotation_state_.annotated_node_id));

  if (!annotated_node) {
    FX_LOGS(ERROR) << "ViewWrapper::DrawHighlight: No node found with id: "
                   << *(annotation_state_.annotated_node_id);
    return;
  }

  auto bounding_box = annotated_node->location();
  annotation_view_->DrawHighlight(bounding_box);
}

void ViewWrapper::HideHighlights() {
  if (!annotation_view_) {
    return;
  }

  annotation_view_->DetachViewContents();
}

std::unique_ptr<ViewWrapper> ViewWrapperFactory::CreateViewWrapper(
    fuchsia::ui::views::ViewRef view_ref, std::unique_ptr<SemanticTreeService> tree_service_ptr,
    fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request) {
  return std::make_unique<ViewWrapper>(std::move(view_ref), std::move(tree_service_ptr),
                                       std::move(semantic_tree_request));
}

}  // namespace a11y
