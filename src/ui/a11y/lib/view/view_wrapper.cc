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
fuchsia::ui::gfx::mat4 MakeTranslationTransform(const fuchsia::ui::gfx::vec3& translation) {
  fuchsia::ui::gfx::mat4 transform;
  transform.matrix[0] = 1;
  transform.matrix[5] = 1;
  transform.matrix[10] = 1;
  transform.matrix[15] = 1;

  transform.matrix[12] = translation.x;
  transform.matrix[13] = translation.y;
  transform.matrix[14] = translation.z;
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
      auto translation_matrix =
          MakeTranslationTransform({-current_node->states().viewport_offset().x,
                                    -current_node->states().viewport_offset().y});
      node_to_root_transform.ChainLocalTransform(translation_matrix);
    }

    if (current_node->has_node_to_container_transform()) {
      // Apply explicit transform.
      node_to_root_transform.ChainLocalTransform(current_node->node_to_container_transform());
    } else if (current_node->has_transform()) {
      node_to_root_transform.ChainLocalTransform(current_node->transform());
    }

    // Once we have applied the root node's tranform, we should exit the loop.
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

      // The `node_to_container_transform` does NOT account for the implied
      // translation with respect to the offset container's bounds, so we must
      // apply that translation explicitly here.
      //
      // NOTE: We do NOT want to apply this translation if:
      //   (1) This node is the root node, OR
      //   (2) This node is its own offset container.
      //
      // We check that the `transform` (deprecated) field is NOT set, as
      // opposed to checking that `node_to_container_transform` IS set, in
      // order to support the transition from `transform` to
      // `node_to_container_transform`. Once the transition is complete,
      // we can remove this condition. There are four cases we need to
      // accommodate:
      //
      //   (1) The client node has an explicit transform AND uses the
      //   `transform` field. In this case, we should not apply the implied
      //   translation here.
      //   (2) The client node does NOT have an explicit trasnform AND uses the
      //   `transform` field.
      //   (3) The client node has an explicit transform AND uses the
      //   `node_to_container_transform` field.
      //   (4) The client node does NOT have an explicit transform AND uses the
      //   `node_to_container_transform` field.
      //
      //   We should only apply the implicit offset in cases (3) and (4). In
      //   in case 4, `node_to_container_transform` will NOT be set, so we can't
      //   simply check that this field is set. Rather, since `transform` will
      //   be unset in both cases (3) and (4), we can use the !has_transform()
      //   condition. Notice that `transform` will always be set in case (1).
      //   It's possible that `transform` is unset in case (2). however, since
      //   the transform only accounts for the translation with respect to the
      //   offset container's bounds in case (2), the only way this field could
      //   be unset is if the offset container's bounds.min is at (0, 0), in
      //   which case applying the implied translation is a NOOP.
      //
      // TODO(fxb.dev/87181): Remove uses of `transform` field.
      if (!current_node->has_transform()) {
        auto container_node = tree_weak_ptr->GetNode(container_id);
        FX_DCHECK(container_node);

        auto translation_matrix = MakeTranslationTransform(container_node->location().min);
        node_to_root_transform.ChainLocalTransform(translation_matrix);
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
                                  transform->translation_vector());
}

void ViewWrapper::ClearAllHighlights() { annotation_view_->ClearAllAnnotations(); }
void ViewWrapper::ClearFocusHighlights() { annotation_view_->ClearFocusHighlights(); }

std::shared_ptr<input::Injector> ViewWrapper::take_view_injector() {
  auto tmp = view_injector_;
  view_injector_.reset();
  return tmp;
}

}  // namespace a11y
