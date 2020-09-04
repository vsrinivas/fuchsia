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
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_manager.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"

namespace accessibility_test {
namespace {

using fuchsia::accessibility::semantics::Attributes;
using fuchsia::accessibility::semantics::Node;
using fuchsia::accessibility::semantics::NodePtr;
using fuchsia::accessibility::semantics::Role;
using fuchsia::accessibility::semantics::SemanticsManager;

class ViewManagerTest : public gtest::TestLoopFixture {
 public:
  ViewManagerTest() = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    tree_service_factory_ = std::make_unique<MockSemanticTreeServiceFactory>();
    tree_service_factory_ptr_ = tree_service_factory_.get();

    auto view_semantics_factory = std::make_unique<MockViewSemanticsFactory>();
    view_semantics_factory_ = view_semantics_factory.get();

    auto annotation_view_factory = std::make_unique<MockAnnotationViewFactory>();
    annotation_view_factory_ = annotation_view_factory.get();

    view_manager_ = std::make_unique<a11y::ViewManager>(
        std::move(tree_service_factory_), std::move(view_semantics_factory),
        std::move(annotation_view_factory), std::make_unique<MockSemanticsEventManager>(),
        context_provider_.context(), debug_dir());
    view_manager_->SetAnnotationsEnabled(true);

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
    auto mock_view_semantics = view_semantics_factory_->GetViewSemantics();
    ASSERT_TRUE(mock_view_semantics);

    auto tree_ptr = mock_view_semantics->GetTree();
    ASSERT_TRUE(tree_ptr);

    ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
    RunLoopUntilIdle();
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> tree_service_factory_;
  std::unique_ptr<a11y::ViewManager> view_manager_;
  std::unique_ptr<MockSemanticProvider> semantic_provider_;
  MockSemanticTreeServiceFactory* tree_service_factory_ptr_;
  MockViewSemanticsFactory* view_semantics_factory_;
  MockAnnotationViewFactory* annotation_view_factory_;
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

TEST_F(ViewManagerTest, SemanticsSourceGetParentNode) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u, 2u, 3u}));
  node_updates.emplace_back(CreateTestNode(1u, "test_label_1"));
  node_updates.emplace_back(CreateTestNode(2u, "test_label_2"));
  node_updates.emplace_back(CreateTestNode(3u, "test_label_3"));
  ApplyNodeUpdates(std::move(node_updates));

  const auto root_node = view_manager_->GetParentNode(semantic_provider_->koid(), 2u);
  const auto null_node = view_manager_->GetParentNode(semantic_provider_->koid(), 0u);

  EXPECT_TRUE(root_node);
  EXPECT_EQ(root_node->node_id(), 0u);
  EXPECT_FALSE(null_node);
}

TEST_F(ViewManagerTest, SemanticsSourceGetNeighboringNodes) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  auto mock_tree = tree_service_factory_ptr_->semantic_tree();
  ASSERT_TRUE(mock_tree);
  auto next_node = CreateTestNode(3u, "test_label_3");
  mock_tree->SetNextNode(&next_node);
  auto previous_node = CreateTestNode(1u, "test_label_1");
  mock_tree->SetPreviousNode(&previous_node);

  const auto returned_next_node = view_manager_->GetNextNode(
      semantic_provider_->koid(), 2u,
      [](const fuchsia::accessibility::semantics::Node* node) { return true; });
  const auto returned_previous_node = view_manager_->GetPreviousNode(
      semantic_provider_->koid(), 2u,
      [](const fuchsia::accessibility::semantics::Node* node) { return true; });

  EXPECT_TRUE(returned_next_node);
  EXPECT_EQ(returned_next_node->node_id(), 3u);
  EXPECT_TRUE(returned_previous_node);
  EXPECT_EQ(returned_previous_node->node_id(), 1u);
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

TEST_F(ViewManagerTest, SemanticsSourcePerformActionFailsBecausePointsToWrongTree) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");
  bool callback_ran = false;
  view_manager_->PerformAccessibilityAction(
      semantic_provider_->koid() +
          1 /*to simulate a koid that does not match the one we are expeting*/,
      0u, fuchsia::accessibility::semantics::Action::DEFAULT, [&callback_ran](bool result) {
        callback_ran = true;
        EXPECT_FALSE(result);
      });

  RunLoopUntilIdle();

  EXPECT_TRUE(callback_ran);
}

TEST_F(ViewManagerTest, FocusHighlightManagerDrawAndClearHighlights) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u}));
  auto node_with_bounding_box = CreateTestNode(1u, "test_label_1");
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  node_with_bounding_box.set_location(bounding_box);
  node_updates.emplace_back(std::move(node_with_bounding_box));
  ApplyNodeUpdates(std::move(node_updates));

  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = semantic_provider_->koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto highlighted_view = annotation_view_factory_->GetAnnotationView(semantic_provider_->koid());
  ASSERT_TRUE(highlighted_view);
  auto highlight = highlighted_view->GetCurrentHighlight();
  EXPECT_TRUE(highlight.has_value());
  EXPECT_EQ(highlight->max.x, 1.0);
  EXPECT_EQ(highlight->max.y, 2.0);
  EXPECT_EQ(highlight->max.z, 3.0);

  view_manager_->ClearHighlight();

  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(semantic_provider_->koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentHighlight();
  EXPECT_FALSE(maybe_highlight.has_value());
}

TEST_F(ViewManagerTest, FocusHighlightManagerDisableAnnotations) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u}));
  auto node_with_bounding_box = CreateTestNode(1u, "test_label_1");
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  node_with_bounding_box.set_location(bounding_box);
  node_updates.emplace_back(std::move(node_with_bounding_box));
  ApplyNodeUpdates(std::move(node_updates));

  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = semantic_provider_->koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto highlighted_view = annotation_view_factory_->GetAnnotationView(semantic_provider_->koid());
  ASSERT_TRUE(highlighted_view);
  auto highlight = highlighted_view->GetCurrentHighlight();
  EXPECT_TRUE(highlight.has_value());
  EXPECT_EQ(highlight->max.x, 1.0);
  EXPECT_EQ(highlight->max.y, 2.0);
  EXPECT_EQ(highlight->max.z, 3.0);

  // Disable annotations.
  view_manager_->SetAnnotationsEnabled(false);

  // Verify that highlights were cleared.
  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(semantic_provider_->koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentHighlight();
  EXPECT_FALSE(maybe_highlight.has_value());
}

TEST_F(ViewManagerTest, FocusHighlightManagerDrawHighlightWithAnnotationsDisabled) {
  view_manager_->SetAnnotationsEnabled(false);
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
  node_updates.emplace_back(CreateTestNode(0u, "test_label_0", {1u}));
  auto node_with_bounding_box = CreateTestNode(1u, "test_label_1");
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  node_with_bounding_box.set_location(bounding_box);
  node_updates.emplace_back(std::move(node_with_bounding_box));
  ApplyNodeUpdates(std::move(node_updates));

  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = semantic_provider_->koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(semantic_provider_->koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentHighlight();
  EXPECT_FALSE(maybe_highlight.has_value());
}

}  // namespace
}  // namespace accessibility_test
