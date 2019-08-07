// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
#define SRC_UI_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/tests/mocks/mock_semantic_action_listener.h"

namespace accessibility_test {

// Mocks Semantics Provider(implemented by Flutter/Chrome) which is responsible for providing
// semantic tree to Semantics Manager.
class MockSemanticProvider {
 public:
  // On initialization, MockSemanticProvider tries to connect to
  // |fuchsia::accessibility::SemanticsManager| service in |context_| and
  // registers with it's view_ref, binding and interface request.
  explicit MockSemanticProvider(sys::ComponentContext* context,
                                fuchsia::ui::views::ViewRef view_ref);
  ~MockSemanticProvider() = default;

  // Calls UpdateSemanticNodes() on SemanticTree with given nodes list.
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes);

  // Calls DeleteSemanticNodes() on SemanticTree with given nodes list.
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids);

  // Calls Commit() on SemanticTree.
  void Commit();

  // Sets hit_test_result in MockSemanticActionListener.
  void SetHitTestResult(uint32_t hit_test_result);

 private:
  // Pointer to semantics manager service.
  fuchsia::accessibility::semantics::SemanticsManagerPtr manager_;

  // Pointer to semantic tree which is used for sending Update/Delete/Commit
  // messages.
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;

  // ViewRef of the Semantic Tree.
  fuchsia::ui::views::ViewRef view_ref_;

  MockSemanticActionListener action_listener_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticProvider);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
