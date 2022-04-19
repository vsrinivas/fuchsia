// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_TESTING_FAKE_A11Y_MANAGER_H_
#define SRC_UI_A11Y_TESTING_FAKE_A11Y_MANAGER_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>
#include <vector>

namespace a11y_testing {

// Trivial semantic tree implementation.
//
// This implementation keeps a semantic tree binding open for its lifetime, and
// responds success unconditionally when clients attempt to commit updates.
class FakeSemanticTree : public fuchsia::accessibility::semantics::SemanticTree {
 public:
  explicit FakeSemanticTree(
      fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener);
  ~FakeSemanticTree() override = default;

  // |fuchsia::accessibility::semantics::SemanticTree|
  void CommitUpdates(CommitUpdatesCallback callback) override;

  // |fuchsia::accessibility::semantics::SemanticTree|
  void UpdateSemanticNodes(std::vector<fuchsia::accessibility::semantics::Node> nodes) override;

  // |fuchsia::accessibility::semantics::SemanticTree|
  void DeleteSemanticNodes(std::vector<uint32_t> node_ids) override;

  void Bind(fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>
                semantic_tree_request);

  void SetSemanticsEnabled(bool enabled);

 private:
  // Unused. We just need to hold onto the bound client end to prevent the peer
  // from receiving ZX_ERR_PEER_CLOSED.
  fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_;
  fidl::Binding<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_binding_;
};

// Trivial accessibility manager implementation.
//
// When a client registers a view for semantics, this class will bind the
// semantic tree and semantic listener channels in the request, and notify the
// client that semantics are disabled. This class will then hold the semantic
// tree and semantic listener channels open until the client closes them.
//
// The fake a11y manager is intended for uses cases where the semantics manager
// service is required, but no accessibility functionality is explicitly
// exercised (e.g. non-a11y tests that run chrome clients).
class FakeA11yManager : public fuchsia::accessibility::semantics::SemanticsManager {
 public:
  FakeA11yManager() = default;
  ~FakeA11yManager() override = default;

  fidl::InterfaceRequestHandler<fuchsia::accessibility::semantics::SemanticsManager> GetHandler();

  // |fuchsia::accessibility::semantics::SemanticsManager|
  void RegisterViewForSemantics(
      fuchsia::ui::views::ViewRef view_ref,
      fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener> handle,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override;

 private:
  fidl::BindingSet<fuchsia::accessibility::semantics::SemanticsManager> semantics_manager_bindings_;
  // We will never need to access any of the semantic trees, so we don't need to
  // associate them with their ViewRefs.
  //
  // Use a std::unique_ptr to prevent any of the FakeSemanticTrees from moving in
  // memory. I believe that would break their bindings, which refer
  // to their initial memory locations.
  std::vector<std::unique_ptr<FakeSemanticTree>> semantic_trees_;
};

}  // namespace a11y_testing

#endif  // SRC_UI_A11Y_TESTING_FAKE_A11Y_MANAGER_H_
