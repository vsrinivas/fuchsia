// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/util/util.h"

namespace accessibility_test {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

class MockSemanticTreeServiceFactory : public a11y::SemanticTreeServiceFactory {
 public:
  std::unique_ptr<a11y::SemanticTreeService> NewService(
      zx_koid_t koid, fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener,
      vfs::PseudoDir* debug_dir,
      a11y::SemanticTreeService::CloseChannelCallback close_channel_callback) override {
    auto service = a11y::SemanticTreeServiceFactory::NewService(
        koid, std::move(semantic_listener), debug_dir, std::move(close_channel_callback));
    service_ = service.get();
    return service;
  }

  a11y::SemanticTreeService* service() { return service_; }

 private:
  a11y::SemanticTreeService* service_ = nullptr;
};

class MockViewWrapper : public a11y::ViewWrapper {
 public:
  MockViewWrapper(
      fuchsia::ui::views::ViewRef view_ref,
      std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request,
      // TODO: Remove default values once user classes have been updated.
      sys::ComponentContext* context = nullptr,
      std::unique_ptr<a11y::AnnotationViewFactoryInterface> annotation_view_factory = nullptr)
      : ViewWrapper(std::move(view_ref), std::move(tree_service_ptr),
                    std::move(semantic_tree_request), context, std::move(annotation_view_factory)) {
  }
  ~MockViewWrapper() override = default;

  void HighlightNode(uint32_t node_id) override { highlighted_node_ = std::make_optional(node_id); }

  void ClearHighlights() override { highlighted_node_ = std::nullopt; }

  std::optional<uint32_t> GetHighlightedNode() const { return highlighted_node_; }

 private:
  std::optional<uint32_t> highlighted_node_;
};

class MockViewWrapperFactory : public a11y::ViewWrapperFactory {
 public:
  MockViewWrapperFactory() = default;
  ~MockViewWrapperFactory() override = default;

  std::unique_ptr<a11y::ViewWrapper> CreateViewWrapper(
      fuchsia::ui::views::ViewRef view_ref,
      std::unique_ptr<a11y::SemanticTreeService> tree_service_ptr,
      fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree> semantic_tree_request)
      override {
    auto koid = a11y::GetKoid(view_ref);
    auto view_wrapper = std::make_unique<MockViewWrapper>(
        std::move(view_ref), std::move(tree_service_ptr), std::move(semantic_tree_request));
    view_wrappers_[koid] = view_wrapper.get();

    return std::move(view_wrapper);
  }

  MockViewWrapper* GetViewWrapper(zx_koid_t koid) { return view_wrappers_[koid]; }

 private:
  std::map<zx_koid_t, MockViewWrapper*> view_wrappers_;
};

class ViewManagerTest : public gtest::TestLoopFixture {
 public:
  ViewManagerTest() = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    tree_service_factory_ = std::make_unique<MockSemanticTreeServiceFactory>();
    tree_service_factory_ptr_ = tree_service_factory_.get();

    view_wrapper_factory_ = std::make_unique<MockViewWrapperFactory>();
    view_wrapper_factory_ptr_ = view_wrapper_factory_.get();

    view_manager_ = std::make_unique<a11y::ViewManager>(
        std::move(tree_service_factory_), std::move(view_wrapper_factory_), debug_dir());

    semantic_provider_ = std::make_unique<MockSemanticProvider>(view_manager_.get());
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  void AddNodeToTree(uint32_t node_id, std::string label,
                     std::vector<uint32_t> child_ids = std::vector<uint32_t>()) {
    std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
    auto node = CreateTestNode(node_id, label, child_ids);
    node_updates.emplace_back(std::move(node));

    ApplyNodeUpdates(std::move(node_updates));
  }

  void ApplyNodeUpdates(std::vector<a11y::SemanticTree::TreeUpdate> node_updates) {
    ASSERT_TRUE(semantic_provider_);

    auto view_wrapper = view_wrapper_factory_ptr_->GetViewWrapper(semantic_provider_->koid());
    ASSERT_TRUE(view_wrapper);

    auto tree_ptr = view_wrapper->GetTree();
    ASSERT_TRUE(tree_ptr);

    ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
    RunLoopUntilIdle();
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> tree_service_factory_;
  std::unique_ptr<a11y::ViewManager> view_manager_;
  std::unique_ptr<MockSemanticProvider> semantic_provider_;
  std::unique_ptr<MockViewWrapperFactory> view_wrapper_factory_;
  MockSemanticTreeServiceFactory* tree_service_factory_ptr_;
  MockViewWrapperFactory* view_wrapper_factory_ptr_;
};

TEST_F(ViewManagerTest, ProviderGetsNotifiedOfSemanticsEnabled) {
  // Enable Semantics Manager.
  view_manager_->SetSemanticsEnabled(true);
  // Upon initialization, MockSemanticProvider calls RegisterViewForSemantics().
  // Ensure that it called the factory to instantiate a new service.
  EXPECT_TRUE(tree_service_factory_ptr_->service());
  RunLoopUntilIdle();

  EXPECT_TRUE(semantic_provider_->GetSemanticsEnabled());

  // Disable Semantics Manager.
  view_manager_->SetSemanticsEnabled(false);
  RunLoopUntilIdle();
  // Semantics Listener should get notified about Semantics manager disable.
  EXPECT_FALSE(semantic_provider_->GetSemanticsEnabled());
}

TEST_F(ViewManagerTest, ClosesChannel) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  EXPECT_TRUE(view_manager_->ViewHasSemantics(semantic_provider_->koid()));

  // Forces the client to disconnect.
  semantic_provider_->SendEventPairSignal();
  RunLoopUntilIdle();

  EXPECT_FALSE(view_manager_->ViewHasSemantics(semantic_provider_->koid()));
}

// Tests that log file is removed when semantic tree service entry is removed from semantics
// manager.
TEST_F(ViewManagerTest, LogFileRemoved) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::string debug_file = std::to_string(semantic_provider_->koid());
  {
    vfs::internal::Node* node;
    EXPECT_EQ(ZX_OK, debug_dir()->Lookup(debug_file, &node));
  }

  // Forces the client to disconnect.
  semantic_provider_->SendEventPairSignal();
  RunLoopUntilIdle();

  // Check Log File is removed.
  {
    vfs::internal::Node* node;
    EXPECT_EQ(ZX_ERR_NOT_FOUND, debug_dir()->Lookup(debug_file, &node));
  }
}

TEST_F(ViewManagerTest, SemanticsSourceViewHasSemantics) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  a11y::SemanticsSource* semantics_source = view_manager_.get();
  EXPECT_TRUE(semantics_source->ViewHasSemantics(a11y::GetKoid(semantic_provider_->view_ref())));

  // Forces the client to disconnect.
  semantic_provider_->SendEventPairSignal();
  RunLoopUntilIdle();
  EXPECT_FALSE(semantics_source->ViewHasSemantics(a11y::GetKoid(semantic_provider_->view_ref())));
}

TEST_F(ViewManagerTest, SemanticsSourceViewRefClone) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  a11y::SemanticsSource* semantics_source = view_manager_.get();
  auto view_ref_or_null =
      semantics_source->ViewRefClone(a11y::GetKoid(semantic_provider_->view_ref()));
  EXPECT_EQ(a11y::GetKoid(semantic_provider_->view_ref()), a11y::GetKoid(*view_ref_or_null));

  // Forces the client to disconnect.
  semantic_provider_->SendEventPairSignal();
  RunLoopUntilIdle();
  // The view is not providing semantics anymore, so there is no return value.
  EXPECT_FALSE(semantics_source->ViewRefClone(a11y::GetKoid(semantic_provider_->view_ref())));
}

TEST_F(ViewManagerTest, SemanticsSourceGetSemanticNode) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");

  const auto node = view_manager_->GetSemanticNode(semantic_provider_->koid(), 0u);
  EXPECT_TRUE(node);
  EXPECT_TRUE(node->has_attributes());
  EXPECT_TRUE(node->attributes().has_label());
  EXPECT_EQ(node->attributes().label(), "test_label");
}

TEST_F(ViewManagerTest, SemanticsSourceGetNeighboringNodes) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u, 2u, 3u}));
  node_updates.emplace_back(CreateTestNode(1u, "test_label_1"));
  node_updates.emplace_back(CreateTestNode(2u, "test_label_2"));
  node_updates.emplace_back(CreateTestNode(3u, "test_label_3"));
  ApplyNodeUpdates(std::move(node_updates));

  const auto next_node = view_manager_->GetNextNode(semantic_provider_->koid(), 2u);
  const auto previous_node = view_manager_->GetPreviousNode(semantic_provider_->koid(), 2u);

  EXPECT_TRUE(next_node);
  EXPECT_EQ(next_node->node_id(), 3u);
  EXPECT_TRUE(previous_node);
  EXPECT_EQ(previous_node->node_id(), 1u);
}

TEST_F(ViewManagerTest, SemanticsSourceHitTest) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");
  semantic_provider_->SetHitTestResult(0u);

  view_manager_->ExecuteHitTesting(semantic_provider_->koid(), fuchsia::math::PointF(),
                                   [](fuchsia::accessibility::semantics::Hit hit) {
                                     EXPECT_TRUE(hit.has_node_id());
                                     EXPECT_EQ(hit.node_id(), 0u);
                                   });

  RunLoopUntilIdle();
}

TEST_F(ViewManagerTest, SemanticsSourcePerformAction) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");

  view_manager_->PerformAccessibilityAction(semantic_provider_->koid(), 0u,
                                            fuchsia::accessibility::semantics::Action::DEFAULT,
                                            [](bool result) { EXPECT_TRUE(result); });

  RunLoopUntilIdle();

  EXPECT_EQ(semantic_provider_->GetRequestedAction(),
            fuchsia::accessibility::semantics::Action::DEFAULT);
  EXPECT_EQ(semantic_provider_->GetRequestedActionNodeId(), 0u);
}

TEST_F(ViewManagerTest, FocusHighlightManagerDrawAndClearHighlights) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u}));
  node_updates.emplace_back(CreateTestNode(1u, "test_label_1"));
  ApplyNodeUpdates(std::move(node_updates));

  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = semantic_provider_->koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto highlighted_view = view_wrapper_factory_ptr_->GetViewWrapper(semantic_provider_->koid());
  ASSERT_TRUE(highlighted_view);
  auto highlighted_node = highlighted_view->GetHighlightedNode();
  EXPECT_TRUE(highlighted_node.has_value());
  EXPECT_EQ(highlighted_node, newly_highlighted_node.node_id);

  view_manager_->ClearHighlight();

  auto maybe_highlighted_view =
      view_wrapper_factory_ptr_->GetViewWrapper(semantic_provider_->koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlighted_node = maybe_highlighted_view->GetHighlightedNode();
  EXPECT_FALSE(maybe_highlighted_node.has_value());
}

}  // namespace accessibility_test
