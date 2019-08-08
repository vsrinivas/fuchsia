// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_BIN_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_
#define SRC_UI_A11Y_BIN_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/vfs/cpp/pseudo_file.h>

#include "src/ui/a11y/bin/a11y_manager/semantics/semantic_tree_impl.h"

namespace a11y_manager {
class SemanticsManagerImpl : public fuchsia::accessibility::semantics::SemanticsManager {
 public:
  SemanticsManagerImpl();
  ~SemanticsManagerImpl() = default;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticsManager> request);

  void SetDebugDirectory(vfs::PseudoDir* debug_dir);

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
      fuchsia::accessibility::semantics::SemanticActionListener::HitTestCallback callback);

 private:
  // |fuchsia::accessibility::semantics::SemanticsManager|:
  void RegisterView(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticActionListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree)
      override;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> bindings_;

  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticTree,
                   std::unique_ptr<SemanticTreeImpl>>
      semantic_tree_bindings_;

  bool enabled_;

  vfs::PseudoDir* debug_dir_ = nullptr;
};
}  // namespace a11y_manager

#endif  // SRC_UI_A11Y_BIN_A11Y_MANAGER_SEMANTICS_SEMANTICS_MANAGER_IMPL_H_
