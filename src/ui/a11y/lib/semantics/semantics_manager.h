// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <zircon/types.h>

#include "src/ui/a11y/lib/semantics/semantic_tree_service.h"

namespace a11y {

class SemanticsManager : public fuchsia::accessibility::semantics::SemanticsManager {
 public:
  explicit SemanticsManager(vfs::PseudoDir* debug_dir);
  ~SemanticsManager() override;

  // Provides the manager a way to query a node if it already knows
  // what view id and node id it wants to query for. This method returns
  // a copy of the queried node. It may return a nullptr if no node is found.
  fuchsia::accessibility::semantics::NodePtr GetAccessibilityNode(
      const fuchsia::ui::views::ViewRef& view_ref, const int32_t node_id);

  // Provides the manager a way to query a node in the semantic tree based on
  // koid(of the ViewRef associated with the semantic tree) and node id.
  // If node is found, this method returns a copy of the queried node, otherwise nullptr is
  // returned.
  fuchsia::accessibility::semantics::NodePtr GetAccessibilityNodeByKoid(const zx_koid_t koid,
                                                                        const int32_t node_id);

  // Function to Enable/Disable Semantics Manager.
  // When Semantics Manager is disabled, all the semantic tree bindings are
  // closed, which deletes all the semantic tree data.
  void SetSemanticsManagerEnabled(bool enabled);

  // Matches ViewRef with given koid, and calls HitTesting() on the matched
  // view.
  // If no view matches given koid, then this function doesn't use callback.
  void PerformHitTesting(
      zx_koid_t koid, ::fuchsia::math::PointF local_point,
      fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback);

 private:
  // |fuchsia::accessibility::semantics::SemanticsManager|:
  void RegisterViewForSemantics(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

  // Helper function for semantic registration.
  void CompleteSemanticRegistration(
      fuchsia::ui::views::ViewRef view_ref,
      fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
          semantic_tree_request);

  // Closes channel for semantic tree that matches the given "koid".
  void CloseChannel(zx_koid_t koid);

  // Helper function to enable semantic updates for all the Views.
  void EnableSemanticsUpdates(bool enabled);

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticTree,
                   std::unique_ptr<SemanticTreeService>>
      semantic_tree_bindings_;

  bool semantics_enabled_ = false;

  vfs::PseudoDir* const debug_dir_;
};
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_SEMANTICS_MANAGER_H_
