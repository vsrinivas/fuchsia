// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <zircon/types.h>

#include <optional>

namespace a11y {

// An interface for a11y query existing semantic information.
// TODO(fxbug.dev/46164): Move all semantic consuming methods from View manager to this interface.
class SemanticsSource {
 public:
  SemanticsSource() = default;
  virtual ~SemanticsSource() = default;

  // Returns true if the view referenced by |view_ref_koid| is providing semantics.
  virtual bool ViewHasSemantics(zx_koid_t view_ref_koid) = 0;

  // Returns a clone of the ViewRef referenced by |view_ref_koid| if it is known.
  // TODO(fxbug.dev/47136): Move ViewRefClone from SemanticsSource to ViewRefWrapper.
  virtual std::optional<fuchsia::ui::views::ViewRef> ViewRefClone(zx_koid_t view_ref_koid) = 0;

  // Returns the semantic node with id |node_id| in the semantic tree with koid |koid|, if one
  // exists. Returns nullptr if |koid| is invalid, or if no node with id |node_id| is found.
  virtual const fuchsia::accessibility::semantics::Node* GetSemanticNode(
      zx_koid_t koid, uint32_t node_id) const = 0;

  // Returns the parent of the node with id |node_id|. Returns nullptr if the
  // intput node is the root.
  virtual const fuchsia::accessibility::semantics::Node* GetParentNode(zx_koid_t koid,
                                                                       uint32_t node_id) const = 0;

  // Returns the next node in traversal-order neighbors relative to the node with id |node_id|.
  virtual const fuchsia::accessibility::semantics::Node* GetNextNode(
      zx_koid_t koid, uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const = 0;

  // Returns the previous node in traversal-order neighbors relative to the node with id |node_id|.
  virtual const fuchsia::accessibility::semantics::Node* GetPreviousNode(
      zx_koid_t koid, uint32_t node_id,
      fit::function<bool(const fuchsia::accessibility::semantics::Node*)> filter) const = 0;

  // Performs a hit test at the point specified by |local_point| within the view corresponding to
  // |koid|. If no such view is found, this function will return without attempting a hit test.
  virtual void ExecuteHitTesting(
      zx_koid_t koid, fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) = 0;

  // Performs accessibility action on node with id |node_id| in view with koid |koid|.
  // If no such view is found, this function will return without attempting a hit test.
  virtual void PerformAccessibilityAction(
      zx_koid_t koid, uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticListener::OnAccessibilityActionRequestedCallback
          callback) = 0;
};

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_SOURCE_H_
