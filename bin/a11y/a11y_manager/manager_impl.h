// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_

#include <unordered_map>

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>

#include "garnet/bin/a11y/a11y_manager/semantic_tree.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"

namespace a11y_manager {

// Accessibility manager interface implementation.
// See manager.fidl for documentation.
class ManagerImpl : public fuchsia::accessibility::Manager {
 public:
  explicit ManagerImpl(component::StartupContext* startup_context,
                       SemanticTree* semantic_tree);
  ~ManagerImpl() = default;

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::accessibility::Manager> request);

 private:
  // |fuchsia::accessibility::Manager|
  void GetHitAccessibilityNode(
      fuchsia::ui::viewsv1::ViewTreeToken token,
      fuchsia::ui::input::PointerEvent input,
      GetHitAccessibilityNodeCallback callback) override;
  void SetAccessibilityFocus(int32_t view_id, int32_t node_id) override;
  void PerformAccessibilityAction(
      fuchsia::accessibility::Action action) override;

  void BroadcastOnNodeAccessibilityAction(
      int32_t id, fuchsia::accessibility::Node node,
      fuchsia::accessibility::Action action);

  component::StartupContext* startup_context_;
  SemanticTree* const semantic_tree_;

  // Temporary solution for view hit testing. View manager should implement
  // the fuchsia::ui::viewsv1::AccessibilityViewInspector interface as
  // an outgoing service. The interface exposes a hit test function that
  // the a11y manager can use to query views hit by a ray in a certain
  // view tree.
  fuchsia::ui::viewsv1::AccessibilityViewInspectorPtr a11y_view_inspector_;

  // Accessibility-focused means that there is a front-end semantics node that
  // currently has accessibility focus.
  bool a11y_focused_;
  // Id specific to a Scenic view.
  int32_t a11y_focused_view_id_;
  // Id specific to a front-end semantic tree node.
  int32_t a11y_focused_node_id_;

  fidl::BindingSet<fuchsia::accessibility::Manager> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ManagerImpl);
};

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_MANAGER_IMPL_H_
