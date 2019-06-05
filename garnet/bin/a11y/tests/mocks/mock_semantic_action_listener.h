// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_ACTION_LISTENER_H_
#define GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_ACTION_LISTENER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace accessibility_test {

class MockSemanticActionListener
    : public fuchsia::accessibility::semantics::SemanticActionListener {
 public:
  // On initialization, MockSemanticActionListener tries to connect to
  // |fuchsia::accessibility::SemanticsManager| service in |context_| and
  // registers with it's view_ref, binding and interface request.
  explicit MockSemanticActionListener(sys::ComponentContext* context,
                                      fuchsia::ui::views::ViewRef view_ref);
  ~MockSemanticActionListener() override = default;

  void UpdateSemanticNodes(
      std::vector<fuchsia::accessibility::semantics::Node> nodes);
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids);
  void Commit();

 private:
  // |fuchsia::accessibility::semantics::SemanticActionListener|
  void OnAccessibilityActionRequested(
      uint32_t node_id, fuchsia::accessibility::semantics::Action action,
      fuchsia::accessibility::semantics::SemanticActionListener::
          OnAccessibilityActionRequestedCallback callback) override {}

  sys::ComponentContext* context_;
  fuchsia::accessibility::semantics::SemanticsManagerPtr manager_;
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticActionListener>
      bindings_;
  fuchsia::ui::views::ViewRef view_ref_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticActionListener);
};

}  // namespace accessibility_test

#endif  // GARNET_BIN_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_ACTION_LISTENER_H_
