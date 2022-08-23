// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/view/view_manager.h"

#include <fuchsia/accessibility/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/fd.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/event.h>

#include <map>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantics_event_manager.h"
#include "src/ui/a11y/lib/testing/view_ref_helper.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_accessibility_view.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_injector_factory.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"
#include "src/ui/input/lib/injector/tests/mocks/mock_injector.h"

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
    auto view_injector_factory = std::make_unique<MockViewInjectorFactory>();
    auto mock_injector = std::make_shared<input_test::MockInjector>();
    mock_injector_ = mock_injector.get();
    view_injector_factory->set_injector(std::move(mock_injector));
    auto accessibility_view = std::make_unique<MockAccessibilityView>();
    // Not initialized, as it is only used to perform a call to a mock.
    fuchsia::ui::views::ViewRef dummy_view_ref;
    std::optional<fuchsia::ui::views::ViewRef> accessibility_view_view_ref =
        std::move(dummy_view_ref);

    accessibility_view->set_view_ref(std::move(accessibility_view_view_ref));

    view_manager_ = std::make_unique<a11y::ViewManager>(
        std::move(tree_service_factory_), std::move(view_semantics_factory),
        std::move(annotation_view_factory), std::move(view_injector_factory),
        std::make_unique<MockSemanticsEventManager>(), std::move(accessibility_view),
        context_provider_.context());
    view_manager_->SetAnnotationsEnabled(true);

    // NOTE: SemanticListener and SemanticTree handles are ignored.
    semantics_manager()->RegisterViewForSemantics(
        view_ref_helper_.Clone(),
        fidl::InterfaceHandle<fuchsia::accessibility::semantics::SemanticListener>(),
        fidl::InterfaceRequest<fuchsia::accessibility::semantics::SemanticTree>());
  }

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

    auto tree_ptr = mock_view_semantics->mock_semantic_tree();
    ASSERT_TRUE(tree_ptr);

    ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
    RunLoopUntilIdle();
  }

  fuchsia::accessibility::semantics::SemanticsManager* semantics_manager() {
    return view_manager_.get();
  }
  fuchsia::accessibility::virtualkeyboard::Registry* keyboard_registry() {
    return view_manager_.get();
  }
  fuchsia::accessibility::virtualkeyboard::Listener* keyboard_listener() {
    return view_manager_.get();
  }

  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> tree_service_factory_;
  std::unique_ptr<a11y::ViewManager> view_manager_;
  MockSemanticTreeServiceFactory* tree_service_factory_ptr_;
  MockViewSemanticsFactory* view_semantics_factory_;
  MockAnnotationViewFactory* annotation_view_factory_;
  input_test::MockInjector* mock_injector_;
  ViewRefHelper view_ref_helper_;
};

TEST_F(ViewManagerTest, SemanticsEnabledAndDisabled) {
  // Enable Semantics Manager.
  view_manager_->SetSemanticsEnabled(true);
  // Upon initialization, MockSemanticProvider calls RegisterViewForSemantics().
  // Ensure that it called the factory to instantiate a new service.
  EXPECT_TRUE(tree_service_factory_ptr_->service());
  RunLoopUntilIdle();

  ASSERT_TRUE(view_semantics_factory_->GetViewSemantics());
  EXPECT_TRUE(view_semantics_factory_->GetViewSemantics()->semantics_enabled());

  // Disable Semantics Manager.
  view_manager_->SetSemanticsEnabled(false);
  RunLoopUntilIdle();
  // Semantics Listener should get notified about Semantics manager disable.
  EXPECT_FALSE(view_semantics_factory_->GetViewSemantics()->semantics_enabled());
}

TEST_F(ViewManagerTest, ClosesChannel) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  EXPECT_TRUE(view_manager_->ViewHasSemantics(view_ref_helper_.koid()));

  // Forces the client to disconnect.
  view_ref_helper_.SendEventPairSignal();
  RunLoopUntilIdle();

  EXPECT_FALSE(view_manager_->ViewHasSemantics(view_ref_helper_.koid()));
}

TEST_F(ViewManagerTest, SemanticsSourceViewHasSemantics) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  a11y::SemanticsSource* semantics_source = view_manager_.get();
  EXPECT_TRUE(semantics_source->ViewHasSemantics(view_ref_helper_.koid()));

  // Forces the client to disconnect.
  view_ref_helper_.SendEventPairSignal();
  RunLoopUntilIdle();
  EXPECT_FALSE(semantics_source->ViewHasSemantics(view_ref_helper_.koid()));
}

TEST_F(ViewManagerTest, SemanticsSourceViewRefClone) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  a11y::SemanticsSource* semantics_source = view_manager_.get();
  auto view_ref_or_null = semantics_source->ViewRefClone(view_ref_helper_.koid());
  EXPECT_EQ(a11y::GetKoid(*view_ref_or_null), view_ref_helper_.koid());

  // Forces the client to disconnect.
  view_ref_helper_.SendEventPairSignal();
  RunLoopUntilIdle();
  // The view is not providing semantics anymore, so there is no return value.
  EXPECT_FALSE(semantics_source->ViewRefClone(view_ref_helper_.koid()));
}

TEST_F(ViewManagerTest, SemanticsSourceGetSemanticNode) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");

  const auto node = view_manager_->GetSemanticNode(view_ref_helper_.koid(), 0u);
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

  const auto root_node = view_manager_->GetParentNode(view_ref_helper_.koid(), 2u);
  const auto null_node = view_manager_->GetParentNode(view_ref_helper_.koid(), 0u);

  EXPECT_TRUE(root_node);
  EXPECT_EQ(root_node->node_id(), 0u);
  EXPECT_FALSE(null_node);
}

TEST_F(ViewManagerTest, SemanticsSourceGetNeighboringNodes) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  auto mock_tree = view_semantics_factory_->GetViewSemantics()->mock_semantic_tree();
  ASSERT_TRUE(mock_tree);
  auto next_node = CreateTestNode(3u, "test_label_3");
  mock_tree->SetNextNode(&next_node);
  auto previous_node = CreateTestNode(1u, "test_label_1");
  mock_tree->SetPreviousNode(&previous_node);

  const auto returned_next_node = view_manager_->GetNextNode(
      view_ref_helper_.koid(), 2u,
      [](const fuchsia::accessibility::semantics::Node* node) { return true; });
  const auto returned_previous_node = view_manager_->GetPreviousNode(
      view_ref_helper_.koid(), 2u,
      [](const fuchsia::accessibility::semantics::Node* node) { return true; });

  EXPECT_TRUE(returned_next_node);
  EXPECT_EQ(returned_next_node->node_id(), 3u);
  EXPECT_TRUE(returned_previous_node);
  EXPECT_EQ(returned_previous_node->node_id(), 1u);
}

TEST_F(ViewManagerTest, SemanticsSourceHitTest) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  const uint32_t kHitTestResult = 1u;

  auto mock_tree = view_semantics_factory_->GetViewSemantics()->mock_semantic_tree();
  ASSERT_TRUE(mock_tree);
  mock_tree->set_hit_testing_handler(
      [](fuchsia::math::PointF local_point,
         fuchsia::accessibility::semantics::SemanticListener::HitTestCallback callback) {
        fuchsia::accessibility::semantics::Hit hit;
        hit.set_node_id(kHitTestResult);
        callback(std::move(hit));
      });

  std::optional<fuchsia::accessibility::semantics::Hit> hit_test_result;
  view_manager_->ExecuteHitTesting(view_ref_helper_.koid(), fuchsia::math::PointF(),
                                   [&hit_test_result](fuchsia::accessibility::semantics::Hit hit) {
                                     hit_test_result = std::move(hit);
                                   });

  ASSERT_TRUE(hit_test_result.has_value());
  ASSERT_TRUE(hit_test_result->has_node_id());
  EXPECT_EQ(hit_test_result->node_id(), kHitTestResult);
}

TEST_F(ViewManagerTest, SemanticsSourcePerformAction) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  std::optional<uint32_t> request_node_id;
  std::optional<fuchsia::accessibility::semantics::Action> request_action;

  auto mock_tree = view_semantics_factory_->GetViewSemantics()->mock_semantic_tree();
  ASSERT_TRUE(mock_tree);
  mock_tree->set_action_handler(
      [&request_node_id, &request_action](uint32_t node_id,
                                          fuchsia::accessibility::semantics::Action action,
                                          fuchsia::accessibility::semantics::SemanticListener::
                                              OnAccessibilityActionRequestedCallback callback) {
        request_node_id = node_id;
        request_action = action;
        callback(true);
      });

  view_manager_->PerformAccessibilityAction(view_ref_helper_.koid(), 0u,
                                            fuchsia::accessibility::semantics::Action::DEFAULT,
                                            [](bool result) { EXPECT_TRUE(result); });

  EXPECT_EQ(request_action, fuchsia::accessibility::semantics::Action::DEFAULT);
  EXPECT_EQ(request_node_id, 0u);
}

TEST_F(ViewManagerTest, SemanticsSourcePerformActionFailsBecausePointsToWrongTree) {
  view_manager_->SetSemanticsEnabled(true);
  RunLoopUntilIdle();

  AddNodeToTree(0u, "test_label");
  bool callback_ran = false;
  view_manager_->PerformAccessibilityAction(
      view_ref_helper_.koid() +
          1 /* to simulate a koid that does not match the one we are expecting */,
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
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto highlighted_view = annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(highlighted_view);
  auto highlight = highlighted_view->GetCurrentFocusHighlight();
  EXPECT_TRUE(highlight.has_value());
  EXPECT_EQ(highlight->max.x, 1.0);
  EXPECT_EQ(highlight->max.y, 2.0);
  EXPECT_EQ(highlight->max.z, 3.0);

  view_manager_->ClearFocusHighlights();

  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentFocusHighlight();
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
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);
  RunLoopUntilIdle();

  auto highlighted_view = annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(highlighted_view);
  auto highlight = highlighted_view->GetCurrentFocusHighlight();
  EXPECT_TRUE(highlight.has_value());
  EXPECT_EQ(highlight->max.x, 1.0);
  EXPECT_EQ(highlight->max.y, 2.0);
  EXPECT_EQ(highlight->max.z, 3.0);

  // Disable annotations.
  view_manager_->SetAnnotationsEnabled(false);

  // Verify that focus highlights were cleared.
  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentFocusHighlight();
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
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 1u;

  view_manager_->UpdateHighlight(newly_highlighted_node);

  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(maybe_highlighted_view);
  auto maybe_highlight = maybe_highlighted_view->GetCurrentFocusHighlight();
  EXPECT_FALSE(maybe_highlight.has_value());
}

TEST_F(ViewManagerTest, HighlightWithTransform) {
  view_manager_->SetAnnotationsEnabled(true);
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

  // Create test nodes.
  fuchsia::ui::gfx::BoundingBox root_bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                     .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto root_node = CreateTestNode(0u, "test_label_0", {1u});
  root_node.set_transform({10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 10, 0, 50, 60, 70, 1});
  root_node.set_location(std::move(root_bounding_box));
  node_updates.emplace_back(std::move(root_node));

  fuchsia::ui::gfx::BoundingBox parent_bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                       .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto parent_node = CreateTestNode(1u, "test_label_1", {2u});
  parent_node.set_transform({2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 1, 1, 1});
  parent_node.set_location(std::move(parent_bounding_box));
  fuchsia::accessibility::semantics::Node parent_copy;
  parent_node.Clone(&parent_copy);  // Creates a copy for latter use.
  node_updates.emplace_back(std::move(parent_node));

  fuchsia::ui::gfx::BoundingBox child_bounding_box = {.min = {.x = 2.0, .y = 3.0, .z = 4.0},
                                                      .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto child_node = CreateTestNode(2u, "test_label_2", {});
  child_node.set_transform({5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 10, 20, 30, 1});
  child_node.set_location(std::move(child_bounding_box));
  node_updates.emplace_back(std::move(child_node));

  ApplyNodeUpdates(std::move(node_updates));
  RunLoopUntilIdle();

  // Highlight node 2.
  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 2u;
  view_manager_->UpdateHighlight(newly_highlighted_node);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  {
    auto maybe_highlighted_view =
        annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
    ASSERT_TRUE(maybe_highlighted_view);
    const auto& highlight_bounding_box = maybe_highlighted_view->GetCurrentFocusHighlight();
    EXPECT_TRUE(highlight_bounding_box.has_value());
    EXPECT_EQ(highlight_bounding_box->min.x, 2.0f);
    EXPECT_EQ(highlight_bounding_box->min.y, 3.0f);
    EXPECT_EQ(highlight_bounding_box->min.z, 4.0f);
    EXPECT_EQ(highlight_bounding_box->max.x, 4.0f);
    EXPECT_EQ(highlight_bounding_box->max.y, 5.0f);
    EXPECT_EQ(highlight_bounding_box->max.z, 6.0f);

    const auto& highlight_translation =
        maybe_highlighted_view->GetFocusHighlightTranslationVector();
    EXPECT_TRUE(highlight_translation.has_value());
    EXPECT_EQ((*highlight_translation)[0], 260.0f);
    EXPECT_EQ((*highlight_translation)[1], 670.0f);
    EXPECT_EQ((*highlight_translation)[2], 1280.0f);

    const auto& highlight_scale = maybe_highlighted_view->GetFocusHighlightScaleVector();
    EXPECT_TRUE(highlight_scale.has_value());
    EXPECT_EQ((*highlight_scale)[0], 100.0f);
    EXPECT_EQ((*highlight_scale)[1], 150.0f);
    EXPECT_EQ((*highlight_scale)[2], 200.0f);
  }

  // Update the parent node to contain an offset. This will cause the child node, when it is
  // highlighted again, to be scrolled in the x and y axis.
  // Note that the scaling for x and y are still present, which are also applied here.
  parent_copy.mutable_states()->set_viewport_offset({.x = 10, .y = 20});
  std::vector<a11y::SemanticTree::TreeUpdate> second_node_updates;
  second_node_updates.emplace_back(std::move(parent_copy));
  ApplyNodeUpdates(std::move(second_node_updates));

  // Highlight node 2 (again).
  view_manager_->UpdateHighlight(newly_highlighted_node);

  {
    // Verify again that the information received was correct, with the difference now that the
    // offset must be applied to the translation vector. The rest stays the same.
    auto maybe_highlighted_view =
        annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
    ASSERT_TRUE(maybe_highlighted_view);
    const auto& highlight_bounding_box = maybe_highlighted_view->GetCurrentFocusHighlight();
    EXPECT_TRUE(highlight_bounding_box.has_value());
    EXPECT_EQ(highlight_bounding_box->min.x, 2.0f);
    EXPECT_EQ(highlight_bounding_box->min.y, 3.0f);
    EXPECT_EQ(highlight_bounding_box->min.z, 4.0f);
    EXPECT_EQ(highlight_bounding_box->max.x, 4.0f);
    EXPECT_EQ(highlight_bounding_box->max.y, 5.0f);
    EXPECT_EQ(highlight_bounding_box->max.z, 6.0f);

    const auto& highlight_translation =
        maybe_highlighted_view->GetFocusHighlightTranslationVector();
    EXPECT_TRUE(highlight_translation.has_value());
    EXPECT_EQ((*highlight_translation)[0], 60.0f);
    EXPECT_EQ((*highlight_translation)[1], 70.0f);
    EXPECT_EQ((*highlight_translation)[2], 1280.0f);  // no change in z axis.

    const auto& highlight_scale = maybe_highlighted_view->GetFocusHighlightScaleVector();
    EXPECT_TRUE(highlight_scale.has_value());
    EXPECT_EQ((*highlight_scale)[0], 100.0f);
    EXPECT_EQ((*highlight_scale)[1], 150.0f);
    EXPECT_EQ((*highlight_scale)[2], 200.0f);
  }
}

TEST_F(ViewManagerTest, HighlightWithTransformAndOffsetContainers) {
  view_manager_->SetAnnotationsEnabled(true);
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

  // Create test nodes.
  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(0u, "test_label_0", {4u});
    node.set_transform({10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 10, 0, 50, 60, 70, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  // This node's transform will be ignored since its child specifies another
  // node as its container.
  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(4u, "test_label_4", {1u});
    node.set_transform({7, 0, 0, 0, 0, 7, 0, 0, 0, 0, 7, 0, 10, 10, 10, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(1u, "test_label_1", {2u});
    node.set_transform({2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 1, 1, 1});
    node.set_location(std::move(bounding_box));
    node.set_container_id(0u);
    node_updates.emplace_back(std::move(node));
  }

  // This node's transform will be ignored since its child specifies another
  // node as its container.
  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(2u, "test_label_2", {3u});
    node.set_transform({20, 0, 0, 0, 0, 20, 0, 0, 0, 0, 20, 0, 5, 10, 15, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 2.0, .y = 3.0, .z = 4.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(3u, "test_label_3", {});
    node.set_transform({5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 10, 20, 30, 1});
    node.set_location(std::move(bounding_box));
    node.set_container_id(1u);
    node_updates.emplace_back(std::move(node));
  }

  ApplyNodeUpdates(std::move(node_updates));
  RunLoopUntilIdle();

  // Highlight node 3.
  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 3u;
  view_manager_->UpdateHighlight(newly_highlighted_node);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(maybe_highlighted_view);
  const auto& highlight_bounding_box = maybe_highlighted_view->GetCurrentFocusHighlight();
  EXPECT_TRUE(highlight_bounding_box.has_value());
  EXPECT_EQ(highlight_bounding_box->min.x, 2.0f);
  EXPECT_EQ(highlight_bounding_box->min.y, 3.0f);
  EXPECT_EQ(highlight_bounding_box->min.z, 4.0f);
  EXPECT_EQ(highlight_bounding_box->max.x, 4.0f);
  EXPECT_EQ(highlight_bounding_box->max.y, 5.0f);
  EXPECT_EQ(highlight_bounding_box->max.z, 6.0f);

  const auto& highlight_translation = maybe_highlighted_view->GetFocusHighlightTranslationVector();
  EXPECT_TRUE(highlight_translation.has_value());
  EXPECT_EQ((*highlight_translation)[0], 260.0f);
  EXPECT_EQ((*highlight_translation)[1], 670.0f);
  EXPECT_EQ((*highlight_translation)[2], 1280.0f);

  const auto& highlight_scale = maybe_highlighted_view->GetFocusHighlightScaleVector();
  EXPECT_TRUE(highlight_scale.has_value());
  EXPECT_EQ((*highlight_scale)[0], 100.0f);
  EXPECT_EQ((*highlight_scale)[1], 150.0f);
}

TEST_F(ViewManagerTest, HighlightWithSelfReferentOffsetContainer) {
  view_manager_->SetAnnotationsEnabled(true);
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

  // Create test nodes.
  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(0u, "test_label_0", {1u});
    node.set_transform({10, 0, 0, 0, 0, 10, 0, 0, 0, 0, 10, 0, 50, 60, 70, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  // This node's offset container is equal to its own node id, so the loop to
  // apply transforms should stop after this node.
  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(1u, "test_label_1", {2u});
    node.set_container_id(1u);
    node.set_transform({7, 0, 0, 0, 0, 8, 0, 0, 0, 0, 9, 0, 10, 10, 10, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  {
    fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                  .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
    auto node = CreateTestNode(2u, "test_label_2");
    node.set_container_id(1u);
    node.set_transform({1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 3, 0, 100, 100, 100, 1});
    node.set_location(std::move(bounding_box));
    node_updates.emplace_back(std::move(node));
  }

  ApplyNodeUpdates(std::move(node_updates));
  RunLoopUntilIdle();

  // Highlight node 2.
  a11y::FocusHighlightManager::SemanticNodeIdentifier newly_highlighted_node;
  newly_highlighted_node.koid = view_ref_helper_.koid();
  newly_highlighted_node.node_id = 2u;
  view_manager_->UpdateHighlight(newly_highlighted_node);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  auto maybe_highlighted_view =
      annotation_view_factory_->GetAnnotationView(view_ref_helper_.koid());
  ASSERT_TRUE(maybe_highlighted_view);
  const auto& highlight_bounding_box = maybe_highlighted_view->GetCurrentFocusHighlight();
  EXPECT_TRUE(highlight_bounding_box.has_value());
  EXPECT_EQ(highlight_bounding_box->min.x, 1.0f);
  EXPECT_EQ(highlight_bounding_box->min.y, 2.0f);
  EXPECT_EQ(highlight_bounding_box->min.z, 3.0f);
  EXPECT_EQ(highlight_bounding_box->max.x, 4.0f);
  EXPECT_EQ(highlight_bounding_box->max.y, 5.0f);
  EXPECT_EQ(highlight_bounding_box->max.z, 6.0f);

  const auto& highlight_translation = maybe_highlighted_view->GetFocusHighlightTranslationVector();
  EXPECT_TRUE(highlight_translation.has_value());
  EXPECT_EQ((*highlight_translation)[0], 710.0f);
  EXPECT_EQ((*highlight_translation)[1], 810.0f);
  EXPECT_EQ((*highlight_translation)[2], 910.0f);

  const auto& highlight_scale = maybe_highlighted_view->GetFocusHighlightScaleVector();
  EXPECT_TRUE(highlight_scale.has_value());
  EXPECT_EQ((*highlight_scale)[0], 7.0f);
  EXPECT_EQ((*highlight_scale)[1], 16.0f);
}

TEST_F(ViewManagerTest, VirtualkeyboardListenerUpdates) {
  EXPECT_FALSE(view_manager_->ViewHasVisibleVirtualkeyboard(view_ref_helper_.koid()));
  EXPECT_FALSE(view_manager_->GetViewWithVisibleVirtualkeyboard());

  fuchsia::accessibility::virtualkeyboard::ListenerPtr keyboard_client;
  keyboard_registry()->Register(view_ref_helper_.Clone(), /* is_visible = */ false,
                                keyboard_client.NewRequest());
  RunLoopUntilIdle();

  EXPECT_TRUE(keyboard_client.is_bound());
  EXPECT_FALSE(view_manager_->ViewHasVisibleVirtualkeyboard(view_ref_helper_.koid()));
  EXPECT_FALSE(view_manager_->GetViewWithVisibleVirtualkeyboard());

  keyboard_listener()->OnVisibilityChanged(true, []() {});
  RunLoopUntilIdle();

  EXPECT_TRUE(view_manager_->ViewHasVisibleVirtualkeyboard(view_ref_helper_.koid()));
  auto invalid_koid = view_ref_helper_.koid() + 1;
  EXPECT_FALSE(view_manager_->ViewHasVisibleVirtualkeyboard(invalid_koid));
  EXPECT_TRUE(view_manager_->GetViewWithVisibleVirtualkeyboard());
  EXPECT_EQ(*view_manager_->GetViewWithVisibleVirtualkeyboard(), view_ref_helper_.koid());

  // Connects a second semantic provider which tries to add a new virtual keyboard listener. This
  // one should fail, as only one registered is supported.
  ViewRefHelper view_ref_helper_2;
  fuchsia::accessibility::virtualkeyboard::ListenerPtr keyboard_client_2;
  keyboard_registry()->Register(view_ref_helper_2.Clone(), /* is_visible = */ true,
                                keyboard_client_2.NewRequest());
  RunLoopUntilIdle();

  EXPECT_FALSE(view_manager_->ViewHasVisibleVirtualkeyboard(view_ref_helper_2.koid()));
  EXPECT_FALSE(keyboard_client_2.is_bound());

  keyboard_listener()->OnVisibilityChanged(false, []() {});
  EXPECT_FALSE(view_manager_->GetViewWithVisibleVirtualkeyboard());
}

TEST_F(ViewManagerTest, InjectorManagerTest) {
  fuchsia::ui::input::InputEvent event;
  EXPECT_FALSE(view_manager_->InjectEventIntoView(event, view_ref_helper_.koid()));
  EXPECT_FALSE(mock_injector_->on_event_called());
  view_manager_->MarkViewReadyForInjection(view_ref_helper_.koid(), true);
  EXPECT_TRUE(view_manager_->InjectEventIntoView(event, view_ref_helper_.koid()));
  EXPECT_TRUE(mock_injector_->on_event_called());
  view_manager_->MarkViewReadyForInjection(view_ref_helper_.koid(), false);
  EXPECT_FALSE(view_manager_->InjectEventIntoView(event, view_ref_helper_.koid()));
}

}  // namespace
}  // namespace accessibility_test
