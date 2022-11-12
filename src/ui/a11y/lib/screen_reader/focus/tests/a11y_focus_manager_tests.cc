// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/executor.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_highlight_delegate.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_registry.h"
#include "src/ui/a11y/lib/focus_chain/tests/mocks/mock_focus_chain_requester.h"
#include "src/ui/a11y/lib/screen_reader/focus/a11y_focus_manager_impl.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree.h"
#include "src/ui/a11y/lib/testing/view_ref_helper.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_semantics.h"
#include "src/ui/a11y/lib/view/tests/mocks/mock_view_source.h"
#include "src/ui/a11y/lib/virtual_keyboard/tests/mocks/mock_virtual_keyboard_manager.h"

namespace accessibility_test {
namespace {

constexpr char kInspectNodeName[] = "test inspector";

constexpr float kEpsilon = 1.e-10f;

enum SceneType { GFX, FLATLAND };

class A11yFocusManagerTest : public gtest::RealLoopFixture,
                             public ::testing::WithParamInterface<SceneType> {
 public:
  A11yFocusManagerTest() : executor_(dispatcher()) {}
  ~A11yFocusManagerTest() override = default;

  void SetUp() override {
    inspector_ = std::make_unique<inspect::Inspector>();
    if (GetParam() == FLATLAND) {
      mock_highlight_delegate_ = std::make_shared<MockHighlightDelegate>();
    }

    a11y_focus_manager_ = std::make_unique<a11y::A11yFocusManagerImpl>(
        &mock_focus_chain_requester_, &mock_focus_chain_registry_, &mock_view_source_,
        &mock_virtual_keyboard_manager_, mock_highlight_delegate_,
        inspector_->GetRoot().CreateChild(kInspectNodeName));
    a11y_focus_manager_->set_on_a11y_focus_updated_callback(
        [this](std::optional<a11y::A11yFocusManager::A11yFocusInfo> focus) {
          a11y_focus_received_in_update_callback_ = std::move(focus);
        });

    // Fill the test view's semantic tree.
    mock_view_source_.CreateView(view_ref_helper_);

    // Create test nodes.
    std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

    fuchsia::ui::gfx::BoundingBox root_bounding_box = {.min = {.x = 0.0, .y = 0.0, .z = 0.0},
                                                       .max = {.x = 0.0, .y = 0.0, .z = 0.0}};
    auto root_node = CreateTestNode(0u, "test_label_0", {1u});
    root_node.set_transform({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 50, 60, 0, 1});  // Translation
    root_node.set_location(std::move(root_bounding_box));
    node_updates.emplace_back(std::move(root_node));

    fuchsia::ui::gfx::BoundingBox parent_bounding_box = {.min = {.x = 0.0, .y = 0.0, .z = 0.0},
                                                         .max = {.x = 0.0, .y = 0.0, .z = 0.0}};
    auto parent_node = CreateTestNode(1u, "test_label_1", {2u});
    parent_node.set_transform({2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1});  // Scale
    parent_node.set_location(std::move(parent_bounding_box));
    fuchsia::accessibility::semantics::Node parent_copy;
    parent_node.Clone(&parent_copy);  // Creates a copy for latter use.
    node_updates.emplace_back(std::move(parent_node));

    fuchsia::ui::gfx::BoundingBox child_bounding_box = {.min = {.x = 2.0, .y = 3.0, .z = 0.0},
                                                        .max = {.x = 4.0, .y = 5.0, .z = 0.0}};
    auto child_node = CreateTestNode(2u, "test_label_2", {});
    child_node.set_transform({1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});  // Identity matrix
    child_node.set_location(std::move(child_bounding_box));
    node_updates.emplace_back(std::move(child_node));

    auto* mock_semantic_tree = GetMockSemanticTree(view_ref_helper_.koid());
    ASSERT_TRUE(mock_semantic_tree);
    mock_semantic_tree->Update(std::move(node_updates));
  }

  // Helper function to check if the given ViewRef has a11y focus.
  void CheckViewInFocus(const ViewRefHelper& view_ref_helper, uint32_t node_id) const {
    auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
    ASSERT_TRUE(a11y_focus.has_value());
    EXPECT_EQ(view_ref_helper.koid(), a11y_focus.value().view_ref_koid);
    EXPECT_EQ(node_id, a11y_focus.value().node_id);
  }

  MockAnnotationView* GetMockAnnotationView(zx_koid_t koid) {
    auto view = mock_view_source_.GetViewWrapper(koid);
    FX_CHECK(view);

    auto* annotation_view = view->annotation_view();
    return static_cast<MockAnnotationView*>(annotation_view);
  }

  MockSemanticTree* GetMockSemanticTree(zx_koid_t koid) {
    auto view = mock_view_source_.GetViewWrapper(koid);
    FX_CHECK(view);

    auto* mock_view_semantics = static_cast<MockViewSemantics*>(view->view_semantics());
    FX_CHECK(mock_view_semantics);

    return mock_view_semantics->mock_semantic_tree();
  }

  void ExpectNoHighlight() {
    if (GetParam() == GFX) {
      auto* mock_annotation_view = GetMockAnnotationView(view_ref_helper_.koid());
      ASSERT_TRUE(mock_annotation_view);
      EXPECT_FALSE(mock_annotation_view->GetCurrentFocusHighlight().has_value());
    } else {
      ASSERT_TRUE(mock_highlight_delegate_);
      EXPECT_FALSE(mock_highlight_delegate_->get_current_highlight().has_value());
    }
  }

  void ExpectSomeHighlight() {
    if (GetParam() == GFX) {
      auto* mock_annotation_view = GetMockAnnotationView(view_ref_helper_.koid());
      ASSERT_TRUE(mock_annotation_view);
      EXPECT_TRUE(mock_annotation_view->GetCurrentFocusHighlight().has_value());
    } else {
      ASSERT_TRUE(mock_highlight_delegate_);
      EXPECT_TRUE(mock_highlight_delegate_->get_current_highlight().has_value());
    }
  }

  void ExpectHighlightGfx(zx_koid_t koid, fuchsia::ui::gfx::BoundingBox bounding_box,
                          std::array<float, 3> scale, std::array<float, 3> translation) {
    ASSERT_EQ(GetParam(), GFX);

    auto* mock_annotation_view = GetMockAnnotationView(koid);
    ASSERT_TRUE(mock_annotation_view);
    ASSERT_TRUE(mock_annotation_view->GetCurrentFocusHighlight().has_value());

    const auto& highlight_bounding_box = mock_annotation_view->GetCurrentFocusHighlight();
    ASSERT_TRUE(highlight_bounding_box.has_value());
    EXPECT_EQ(highlight_bounding_box->min.x, bounding_box.min.x);
    EXPECT_EQ(highlight_bounding_box->min.y, bounding_box.min.y);
    EXPECT_EQ(highlight_bounding_box->min.z, bounding_box.min.z);
    EXPECT_EQ(highlight_bounding_box->max.x, bounding_box.max.x);
    EXPECT_EQ(highlight_bounding_box->max.y, bounding_box.max.y);
    EXPECT_EQ(highlight_bounding_box->max.z, bounding_box.max.z);

    const auto& highlight_scale = mock_annotation_view->GetFocusHighlightScaleVector();
    ASSERT_TRUE(highlight_scale.has_value());
    EXPECT_EQ(*highlight_scale, scale);

    const auto& highlight_translation = mock_annotation_view->GetFocusHighlightTranslationVector();
    ASSERT_TRUE(highlight_translation.has_value());
    EXPECT_EQ(*highlight_translation, translation);
  }

  void ExpectHighlightFlatland(const MockHighlightDelegate::Highlight& highlight) {
    ASSERT_EQ(GetParam(), FLATLAND);

    ASSERT_TRUE(mock_highlight_delegate_);
    ASSERT_TRUE(mock_highlight_delegate_->get_current_highlight().has_value());

    auto other = mock_highlight_delegate_->get_current_highlight().value();
    EXPECT_NEAR(highlight.top_left.x, other.top_left.x, kEpsilon);
    EXPECT_NEAR(highlight.top_left.y, other.top_left.y, kEpsilon);
    EXPECT_NEAR(highlight.bottom_right.x, other.bottom_right.x, kEpsilon);
    EXPECT_NEAR(highlight.bottom_right.y, other.bottom_right.y, kEpsilon);
    EXPECT_EQ(highlight.view_koid, other.view_koid);
  }

  // Helper function to ensure that a promise completes.
  void RunPromiseToCompletion(fpromise::promise<> promise) {
    bool done = false;
    executor_.schedule_task(std::move(promise).and_then([&]() { done = true; }));
    RunLoopUntil([&] { return done; });
  }

  ViewRefHelper view_ref_helper_;
  MockViewSource mock_view_source_;
  MockAccessibilityFocusChainRequester mock_focus_chain_requester_;
  MockAccessibilityFocusChainRegistry mock_focus_chain_registry_;
  MockVirtualKeyboardManager mock_virtual_keyboard_manager_;
  std::shared_ptr<MockHighlightDelegate> mock_highlight_delegate_;
  std::optional<a11y::A11yFocusManager::A11yFocusInfo> a11y_focus_received_in_update_callback_;
  std::unique_ptr<inspect::Inspector> inspector_;
  std::unique_ptr<a11y::A11yFocusManager> a11y_focus_manager_;
  async::Executor executor_;
};

// GetA11yFocus() doesn't return anything when no view is in focus.
TEST_P(A11yFocusManagerTest, GetA11yFocusNoViewFound) {
  // By default no view is in a11y focus.
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
  EXPECT_FALSE(a11y_focus_received_in_update_callback_);
}

TEST_P(A11yFocusManagerTest, ChangingA11yFocusCausesAFocusChainUpdate) {
  mock_focus_chain_requester_.set_will_change_focus(true);

  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);
  EXPECT_TRUE(a11y_focus_received_in_update_callback_);
  EXPECT_EQ(a11y_focus_received_in_update_callback_->view_ref_koid, view_ref_helper_.koid());
  EXPECT_EQ(a11y_focus_received_in_update_callback_->node_id, 2u);

  // Check that the highlight was drawn.
  ExpectSomeHighlight();

  // Now that one view is in Focus, changes the focus to another view, which causes again a Focus
  // Chain update.
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper_2;
  mock_view_source_.CreateView(view_ref_helper_2);
  bool success_2 = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u,
                                    [&success_2](bool result) { success_2 = result; });
  CheckViewInFocus(view_ref_helper_2, 1u);
  EXPECT_TRUE(success_2);
  EXPECT_TRUE(a11y_focus_received_in_update_callback_);
  EXPECT_EQ(a11y_focus_received_in_update_callback_->view_ref_koid, view_ref_helper_2.koid());
  EXPECT_EQ(a11y_focus_received_in_update_callback_->node_id, 1u);
  EXPECT_TRUE(mock_focus_chain_requester_.ReceivedViewRef());
  EXPECT_EQ(a11y::GetKoid(*mock_focus_chain_requester_.ReceivedViewRef()),
            view_ref_helper_2.koid());

  // Check that the highlight in the originally focused view is cleared.
  ExpectNoHighlight();
}

TEST_P(A11yFocusManagerTest, ChangingA11yFocusCausesAnInspectUpdate) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(),
                                    a11y::A11yFocusManagerImpl::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, a11y::A11yFocusManagerImpl::kRootNodeId);
  EXPECT_TRUE(success);
  // Now that one view is in Focus, changes the focus to another view, which causes again a Focus
  // Chain update.
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper_2;
  mock_view_source_.CreateView(view_ref_helper_2);
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u, [](bool result) {});

  fpromise::result<inspect::Hierarchy> hierarchy;
  RunPromiseToCompletion(inspect::ReadFromInspector(*inspector_)
                             .then([&](fpromise::result<inspect::Hierarchy>& result) {
                               hierarchy = std::move(result);
                             }));
  ASSERT_TRUE(hierarchy.is_ok());

  auto* test_inspect_hierarchy = hierarchy.value().GetByPath({kInspectNodeName});
  ASSERT_TRUE(test_inspect_hierarchy);
  auto* focused_koid = test_inspect_hierarchy->node().get_property<inspect::UintPropertyValue>(
      a11y::A11yFocusManagerImpl::kCurrentlyFocusedKoidInspectNodeName);
  ASSERT_EQ(focused_koid->value(), view_ref_helper_2.koid());
  auto* focused_node_id = test_inspect_hierarchy->node().get_property<inspect::UintPropertyValue>(
      a11y::A11yFocusManagerImpl::kCurrentlyFocusedNodeIdInspectNodeName);
  ASSERT_EQ(focused_node_id->value(), 1u);
}

TEST_P(A11yFocusManagerTest, ChangingA11yFocusCausesAFailedFocusChainUpdate) {
  mock_focus_chain_requester_.set_will_change_focus(false);
  bool success = true;  // expects false later.
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(),
                                    a11y::A11yFocusManagerImpl::kRootNodeId,
                                    [&success](bool result) { success = result; });
  EXPECT_FALSE(success);
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus.has_value());
}

TEST_P(A11yFocusManagerTest, ChangingA11yFocusToTheSameView) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(),
                                    a11y::A11yFocusManagerImpl::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, a11y::A11yFocusManagerImpl::kRootNodeId);
  EXPECT_TRUE(success);
  EXPECT_TRUE(a11y_focus_received_in_update_callback_);
  EXPECT_EQ(a11y_focus_received_in_update_callback_->view_ref_koid, view_ref_helper_.koid());
  EXPECT_EQ(a11y_focus_received_in_update_callback_->node_id,
            a11y::A11yFocusManagerImpl::kRootNodeId);

  // Changes the focus to the same view.
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success_2 = false;
  mock_focus_chain_requester_.clear_view_ref();
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 1u,
                                    [&success_2](bool result) { success_2 = result; });
  CheckViewInFocus(view_ref_helper_, 1u);
  EXPECT_TRUE(success_2);
  EXPECT_TRUE(a11y_focus_received_in_update_callback_);
  EXPECT_EQ(a11y_focus_received_in_update_callback_->view_ref_koid, view_ref_helper_.koid());
  EXPECT_EQ(a11y_focus_received_in_update_callback_->node_id, 1u);
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());
}

TEST_P(A11yFocusManagerTest, ChangingA11yFocusToTheViewThatHasInputFocus) {
  // The Focus Chain is updated and the Focus Chain Manager listens to the update.
  mock_focus_chain_registry_.SendViewRefKoid(view_ref_helper_.koid());
  CheckViewInFocus(view_ref_helper_, a11y::A11yFocusManagerImpl::kRootNodeId);

  // A client requests to set a11y focus to the same view that already has input focus.
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(),
                                    a11y::A11yFocusManagerImpl::kRootNodeId,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, a11y::A11yFocusManagerImpl::kRootNodeId);
  EXPECT_TRUE(success);

  // The Focus Chain Manager does not need to request a focus chain update.
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());
}

TEST_P(A11yFocusManagerTest, NoFocusChangeIfViewRefMissing) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Request to transfer focus to view 2 without first creating the view.
  mock_focus_chain_requester_.set_will_change_focus(true);
  ViewRefHelper view_ref_helper_2;
  mock_focus_chain_requester_.clear_view_ref();
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u, [](bool result) {});

  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());
}

TEST_P(A11yFocusManagerTest, NoFocusChainUpdateToVirtualKeyboardView) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Request to transfer focus to view 2, which has a visible virtual keyboard.
  ViewRefHelper view_ref_helper_2;
  mock_view_source_.CreateView(view_ref_helper_2);
  mock_virtual_keyboard_manager_.set_view_with_virtual_keyboard(view_ref_helper_2.koid());
  mock_focus_chain_requester_.clear_view_ref();
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u, [](bool result) {});

  CheckViewInFocus(view_ref_helper_2, 1u);
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());
}

TEST_P(A11yFocusManagerTest, NoFocusChainUpdateToVirtualKeyboardViewAndBack) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  {
    bool success = false;
    a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                      [&success](bool result) { success = result; });
    CheckViewInFocus(view_ref_helper_, 2u);
    EXPECT_TRUE(success);
  }

  // Request to transfer focus to view 2, which has a visible virtual keyboard.
  ViewRefHelper view_ref_helper_2;
  mock_view_source_.CreateView(view_ref_helper_2);
  mock_virtual_keyboard_manager_.set_view_with_virtual_keyboard(view_ref_helper_2.koid());
  mock_focus_chain_requester_.clear_view_ref();
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u, [](bool result) {});

  CheckViewInFocus(view_ref_helper_2, 1u);
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());

  // Request to transfer focus back to view 1.
  {
    bool success = false;
    a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                      [&success](bool result) { success = result; });
    CheckViewInFocus(view_ref_helper_, 2u);
    EXPECT_TRUE(success);
  }

  // Because input focus remained on view 1, we don't have to send a focus chain
  // update when switching back to it.
  EXPECT_FALSE(mock_focus_chain_requester_.ReceivedViewRef());
}

TEST_P(A11yFocusManagerTest, RestoresA11yFocusToInputFocus) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Request to transfer focus to view 2, which has a visible virtual keyboard.
  ViewRefHelper view_ref_helper_2;
  mock_view_source_.CreateView(view_ref_helper_2);
  mock_virtual_keyboard_manager_.set_view_with_virtual_keyboard(view_ref_helper_2.koid());
  mock_focus_chain_requester_.clear_view_ref();
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_2.koid(), 1u, [](bool result) {});

  CheckViewInFocus(view_ref_helper_2, 1u);

  // Restore focus to the view with input focus.
  a11y_focus_manager_->RestoreA11yFocusToInputFocus();
  CheckViewInFocus(view_ref_helper_, 2u);
}

TEST_P(A11yFocusManagerTest, ListensToFocusChainUpdates) {
  // The Focus Chain is updated and the Focus Chain Manager listens to the update.
  mock_focus_chain_registry_.SendViewRefKoid(view_ref_helper_.koid());
  CheckViewInFocus(view_ref_helper_, a11y::A11yFocusManagerImpl::kRootNodeId);
}

TEST_P(A11yFocusManagerTest, ClearsTheA11YFocus) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Check that the highlight was drawn.
  ExpectSomeHighlight();

  a11y_focus_manager_->ClearA11yFocus();
  auto a11y_focus = a11y_focus_manager_->GetA11yFocus();
  ASSERT_FALSE(a11y_focus);
  EXPECT_FALSE(a11y_focus_received_in_update_callback_);
  ExpectNoHighlight();
}

TEST_P(A11yFocusManagerTest, DeletingA11yFocusManagerClearsHighlights) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Check that the highlight was drawn.
  ExpectSomeHighlight();

  // Delete the a11y focus manager object.
  a11y_focus_manager_.reset();

  // Verify that the highlight was cleared.
  ExpectNoHighlight();
}

INSTANTIATE_TEST_SUITE_P(GfxAndFlatland, A11yFocusManagerTest, ::testing::Values(GFX, FLATLAND));

class GfxOnlyTest : public A11yFocusManagerTest {};

INSTANTIATE_TEST_SUITE_P(GfxOnlyFocusManagerTests, GfxOnlyTest, ::testing::Values(GFX));

TEST_P(GfxOnlyTest, DrawsCorrectHighlights) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Check that the highlight is positioned correctly.
  ExpectHighlightGfx(
      view_ref_helper_.koid(), /* bounding box = */
      {.min = {.x = 2.0f, .y = 3.0, .z = 0.0}, .max = {.x = 4.0, .y = 5.0, .z = 0.0}},
      /* scale = */
      {2.f, 2.f, 2.f},
      /* translation = */ {50.f, 60.f, 0.f});
}

class FlatlandOnlyTest : public A11yFocusManagerTest {};

INSTANTIATE_TEST_SUITE_P(FlatlandOnlyFocusManagerTests, FlatlandOnlyTest,
                         ::testing::Values(FLATLAND));

TEST_P(FlatlandOnlyTest, DrawsCorrectHighlights) {
  mock_focus_chain_requester_.set_will_change_focus(true);
  bool success = false;
  a11y_focus_manager_->SetA11yFocus(view_ref_helper_.koid(), 2u,
                                    [&success](bool result) { success = result; });
  CheckViewInFocus(view_ref_helper_, 2u);
  EXPECT_TRUE(success);

  // Check that the highlight is positioned correctly.
  ExpectHighlightFlatland({{54, 66}, {58, 70}, view_ref_helper_.koid()});
}

}  // namespace
}  // namespace accessibility_test
