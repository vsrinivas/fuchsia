// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "view_wrapper.h"

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <stack>

#include "src/ui/a11y/lib/semantics/util/semantic_transform.h"

namespace a11y {
namespace {

// Builds a transform of the form:
// | 1  0  0  Tx |
// | 0  1  0  Ty |
// | 0  0  1  0 |
// | 0  0  0  1  |
// Where: Tx and Ty come from |offset|.
fuchsia::ui::gfx::mat4 MakeTranslationTransform(const fuchsia::ui::gfx::vec2& offset) {
  fuchsia::ui::gfx::mat4 transform;
  transform.matrix[0] = 1;
  transform.matrix[5] = 1;
  transform.matrix[10] = 1;
  transform.matrix[15] = 1;

  transform.matrix[12] = -offset.x;
  transform.matrix[13] = -offset.y;
  return transform;
}

}  // namespace

ViewWrapper::ViewWrapper(fuchsia::ui::views::ViewRef view_ref,
                         std::unique_ptr<ViewSemantics> view_semantics,
                         std::unique_ptr<AnnotationViewInterface> annotation_view)
    : view_ref_(std::move(view_ref)),
      view_semantics_(std::move(view_semantics)),
      annotation_view_(std::move(annotation_view)) {}

void ViewWrapper::EnableSemanticUpdates(bool enabled) {
  view_semantics_->EnableSemanticUpdates(enabled);
}

fxl::WeakPtr<::a11y::SemanticTree> ViewWrapper::GetTree() const {
  return view_semantics_->GetTree();
}

fuchsia::ui::views::ViewRef ViewWrapper::ViewRefClone() const { return Clone(view_ref_); }

void ViewWrapper::HighlightMagnificationViewport(float magnification_scale,
                                                 float magnification_translation_x,
                                                 float magnification_translation_y) {
  auto tree_weak_ptr = GetTree();

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "ViewWrapper::DrawHighlight: Invalid tree pointer";
    return;
  }

  // We need to get the bounds of the view's root node, so retrieve the root
  // node.
  auto root_node = tree_weak_ptr->GetNode(0u);

  FX_DCHECK(root_node);

  auto root_node_bounding_box = root_node->location();

  // Get the dimensions of the root node's bounding box. We will use these to
  // compute the dimensions of the magnification viewport later.
  auto width = root_node_bounding_box.max.x - root_node_bounding_box.min.x;
  auto height = root_node_bounding_box.max.y - root_node_bounding_box.min.y;

  // Get the "top left" or "minimum" in NDC for the magnification viewport.
  // Note that the local coordinate space for this view is rotated 90 degrees
  // clockwise from NDC, so the "top left" corner of the screen is actually the
  // "bottom left" corenr in NDC. So, the "top left" corner of the screen is
  // at point (-1, 1) in NDC.
  // We want to determine which NDC point in unmagnified space will be located
  // at (-1, 1) in NDC (this point will be the "min" of the bounding box for
  // the magnifier viewport in NDC. Here, we are essentially applying the
  // inverse of the magnification transform to the point (-1, 1).
  auto x_top_left_ndc = (-1 - magnification_translation_x) / magnification_scale;
  auto y_top_left_ndc = (1 - magnification_translation_y) / magnification_scale;

  // Now, convert the NDC location of the upper left corner of the magnification
  // viewport to local coordinates. NDC point (0, 0) will be in the center of
  // the view -- (root_node_bounding_box.min.x + (width / 2),
  // root_node_bounding_box.min.y + (height / 2)). Furthermore, since NDC
  // coordinates fall between -1 and 1, the conversion factor for NDC to local
  // is just (width or height) / 2.
  // NOTE: Since the local space is rotated relative to NDC, we need to switch
  // the x- and y- coordinates (i.e. use the y NDC coordinate to compute the local
  // x and vice versa). We also need to use the opposite of the y coordinate to
  // account for the rotation of the screen.
  auto x_translation = root_node_bounding_box.min.x + (width / 2) + (width / 2) * -y_top_left_ndc;
  auto y_translation = root_node_bounding_box.min.y + (height / 2) + (height / 2) * x_top_left_ndc;

  // Finally, compute the bounds of the magnification viewport in local
  // coordinates.
  fuchsia::ui::gfx::BoundingBox magnification_viewport_bounding_box;
  magnification_viewport_bounding_box.min.x = x_translation;
  magnification_viewport_bounding_box.min.y = y_translation;
  magnification_viewport_bounding_box.max.x =
      magnification_viewport_bounding_box.min.x + (width / magnification_scale);
  magnification_viewport_bounding_box.max.y =
      magnification_viewport_bounding_box.min.y + (height / magnification_scale);

  // Compute the local->global coordinate transform, which will just be the
  // root node's transform since the root node doesn't have a parent.
  SemanticTransform transform;
  if (root_node->has_transform()) {
    transform.ChainLocalTransform(root_node->transform());
  }

  annotation_view_->DrawHighlight(magnification_viewport_bounding_box, transform.scale_vector(),
                                  transform.translation_vector(),
                                  true /* is_magnification_highlight */);
}

std::optional<SemanticTransform> ViewWrapper::GetNodeToRootTransform(uint32_t node_id) const {
  auto tree_weak_ptr = GetTree();

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "Invalid tree pointer";
    return std::nullopt;
  }

  auto* node = tree_weak_ptr->GetNode(node_id);

  if (!node) {
    FX_LOGS(ERROR) << "No node found iwth id: " << node_id;
    return std::nullopt;
  }

  // Compute the translation and scaling vectors for the node's bounding box.
  // Each node can supply a 4x4 transform matrix of the form:
  // [ Sx   0    0    Tx ]
  // [ 0    Sy   0    Ty ]
  // [ 0    0    Sz   Tz ]
  // [ 0    0    0    1  ]
  //
  // Here, Sx, Sy, and Sz are the scale coefficients on the x, y, and z axes,
  // respectively. Tx, Ty, and Tz are the x, y, and z components of translation,
  // respectively.
  //
  // In order to compute the transform matrix from the focused node's coordinate
  // space to the root node's coordinate space, we can simply compute the
  // cross product of the focused node's ancestors' transform matrices,
  // beginning at the focused node and up to the minimum-depth non-root ancestor
  // (the root does not have a parent, so it does not need a transform).
  //
  // [Focused node to scenic view] = [root transform] x [depth 1 ancestor transform] x
  //   [depth 2 ancestor transform] x ...  x [parent transform] x [focused node transform]
  //
  // The resulting transform will be of the same form as described above. Using
  // this matrix, we can simply extract the scaling and translation vectors
  // required by scenic: (Sx, Sy, Sz) and (Tx, Ty, Tz), respectively.
  //
  // Note that if a node has scroll offsets, it introduces a transform matrix filling only the
  // translation values to account for the scrolling. This transform is part of the computation
  // described above.

  uint32_t current_node_id = node_id;
  SemanticTransform node_to_root_transform;
  while (true) {
    auto current_node = tree_weak_ptr->GetNode(current_node_id);
    FX_DCHECK(current_node);
    // Don't apply scrolling that's on the target node, since scrolling affects
    // the location of its children rather than it.  Apply scrolling before the
    // node's transform, since the scrolling moves its children within it and
    // then the transform moves the result to the parent's space.
    if (current_node_id != node_id && current_node->has_states() &&
        current_node->states().has_viewport_offset()) {
      auto translation_matrix = MakeTranslationTransform(current_node->states().viewport_offset());
      node_to_root_transform.ChainLocalTransform(translation_matrix);
    }
    if (current_node->has_transform()) {
      node_to_root_transform.ChainLocalTransform(current_node->transform());
    }

    // Once we have applied the root node's tranform, we shoud exit the loop.
    if (current_node_id == 0) {
      break;
    }

    // If |current_node| has an offset container specified, then its transform
    // puts local coordinates into the coordinate space of the offset container
    // node, NOT the parent of |current_node|. If no offset container is
    // specified, then we assume the transform is relative to the parent.
    if (current_node->has_container_id()) {
      const auto container_id = current_node->container_id();

      // It's possible for a node to have a container id equal to its own id.
      // In this case, this node's coordinate space will be equivalent to
      // "root" space, so we should terminate the loop here.
      if (container_id == current_node_id) {
        break;
      }

      current_node_id = container_id;
    } else {
      auto parent_node = tree_weak_ptr->GetParentNode(current_node_id);
      FX_DCHECK(parent_node);
      current_node_id = parent_node->node_id();
    }
  }

  return node_to_root_transform;
}

void ViewWrapper::HighlightNode(uint32_t node_id) {
  auto tree_weak_ptr = GetTree();

  if (!tree_weak_ptr) {
    FX_LOGS(ERROR) << "Invalid tree pointer";
    return;
  }

  auto annotated_node = tree_weak_ptr->GetNode(node_id);

  if (!annotated_node) {
    FX_LOGS(ERROR) << "No node found with id: " << node_id;
    return;
  }

  auto transform = GetNodeToRootTransform(node_id);
  if (!transform) {
    FX_LOGS(ERROR) << "Could not compute node-to-root transform for node: " << node_id;
    return;
  }

  auto bounding_box = annotated_node->location();
  annotation_view_->DrawHighlight(bounding_box, transform->scale_vector(),
                                  transform->translation_vector(),
                                  false /* is_magnification_highlight */);
}

void ViewWrapper::ClearAllHighlights() { annotation_view_->ClearAllAnnotations(); }
void ViewWrapper::ClearFocusHighlights() { annotation_view_->ClearFocusHighlights(); }
void ViewWrapper::ClearMagnificationHighlights() {
  annotation_view_->ClearMagnificationHighlights();
}

std::shared_ptr<input::Injector> ViewWrapper::take_view_injector() {
  auto tmp = view_injector_;
  view_injector_.reset();
  return tmp;
}

}  // namespace a11y
