// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"

namespace accessibility_test {

// Mocks Semantics Provider(implemented by Flutter/Chrome) which is responsible for providing
// semantic tree to Semantics Manager.
class MockSemanticProvider {
 public:
  // On initialization, MockSemanticProvider tries to connect to
  // |fuchsia::accessibility::SemanticsManager| service in |context_| and
  // registers with it's view_ref, binding and interface request.
  explicit MockSemanticProvider(fuchsia::accessibility::semantics::SemanticsManager* manager);
  ~MockSemanticProvider() = default;

  const fuchsia::ui::views::ViewRef& view_ref() const { return view_ref_; };

  // Calls UpdateSemanticNodes() on SemanticTree with given nodes list.
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes);

  // Calls DeleteSemanticNodes() on SemanticTree with given nodes list.
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids);

  // Calls Commit() Updates.
  void CommitUpdates();

  // Sets hit_test_result in MockSemanticListener.
  void SetHitTestResult(uint32_t hit_test_result);

  // Returns Commit Failed status.
  bool CommitFailedStatus() { return commit_failed_; };

  // Returns Semantics Enabled field from Semantic Listener.
  bool GetSemanticsEnabled();

  // Function for sending signal to the view ref peer.
  void SendEventPairSignal();

  fuchsia::ui::views::ViewRef CreateOrphanViewRef();

  // Returns true if channel is closed.
  bool IsChannelClosed();

 private:
  // Pointer to semantic tree which is used for sending Update/Delete/Commit
  // messages.
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;

  // ViewRef of the Semantic Tree.
  zx::eventpair eventpair_peer_;
  fuchsia::ui::views::ViewRef view_ref_;

  bool commit_failed_;
  MockSemanticListener semantic_listener_;
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticListener> semantic_listener_bindings_;
  FXL_DISALLOW_COPY_AND_ASSIGN(MockSemanticProvider);
};

}  // namespace accessibility_test

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TESTS_MOCKS_MOCK_SEMANTIC_PROVIDER_H_
