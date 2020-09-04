// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/bin/a11y_manager/tests/util/util.h"
#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_tree_service_factory.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/a11y_view_semantics.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {
namespace {

class MockSemanticTreeService : public a11y::SemanticTreeService {
  void EnableSemanticsUpdates(bool enabled) { enabled_ = enabled; }

  bool UpdatesEnabled() { return enabled_; }

 private:
  bool enabled_ = false;
};

class ViewWrapperTest : public gtest::TestLoopFixture {
 public:
  ViewWrapperTest() {}

  void SetUp() override {
    TestLoopFixture::SetUp();

    semantic_tree_service_factory_ = std::make_unique<MockSemanticTreeServiceFactory>();

    mock_semantic_listener_ = std::make_unique<MockSemanticListener>();
    semantic_listener_binding_ =
        std::make_unique<fidl::Binding<fuchsia::accessibility::semantics::SemanticListener>>(
            mock_semantic_listener_.get());

    koid_ = a11y::GetKoid(view_ref_);

    fuchsia::accessibility::semantics::SemanticListenerPtr semantic_listener_ptr;
    auto tree_service = semantic_tree_service_factory_->NewService(
        koid_, std::move(semantic_listener_ptr),
        context_provider_.context()->outgoing()->debug_dir(), [](zx_status_t status) {},
        [](a11y::SemanticsEventType event_type) {});
    tree_service_ = tree_service.get();

    auto view_semantics =
        std::make_unique<a11y::A11yViewSemantics>(std::move(tree_service), tree_ptr_.NewRequest());
    auto annotation_view = std::make_unique<MockAnnotationView>([]() {}, []() {}, []() {});
    annotation_view->InitializeView(fuchsia::ui::views::ViewRef() /*unused*/);
    annotation_view_ = annotation_view.get();
    ASSERT_TRUE(annotation_view_);
    EXPECT_TRUE(annotation_view_->IsInitialized());

    view_wrapper_ = std::make_unique<a11y::ViewWrapper>(
        std::move(view_ref_), std::move(view_semantics), std::move(annotation_view));

    view_wrapper_->EnableSemanticUpdates(true);
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> semantic_tree_service_factory_;
  std::unique_ptr<MockSemanticListener> mock_semantic_listener_;
  std::unique_ptr<fidl::Binding<fuchsia::accessibility::semantics::SemanticListener>>
      semantic_listener_binding_;
  std::unique_ptr<a11y::ViewWrapper> view_wrapper_;
  MockAnnotationView* annotation_view_;
  a11y::SemanticTreeService* tree_service_;
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;
  fuchsia::ui::views::ViewRef view_ref_;
  zx_koid_t koid_;
};

TEST_F(ViewWrapperTest, HighlightAndClear) {
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

  // Create test nodes.
  fuchsia::ui::gfx::BoundingBox root_bounding_box = {.min = {.x = 0.0, .y = 0.0, .z = 0.0},
                                                     .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  auto root_node = CreateTestNode(0u, "test_label_0", {});
  root_node.set_location(std::move(root_bounding_box));
  node_updates.emplace_back(std::move(root_node));

  auto tree_ptr = tree_service_->Get();
  ASSERT_TRUE(tree_ptr);

  ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
  RunLoopUntilIdle();

  // Highlight node 0.
  view_wrapper_->HighlightNode(0u);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& highlight_bounding_box = annotation_view_->GetCurrentHighlight();
  EXPECT_TRUE(highlight_bounding_box.has_value());
  EXPECT_EQ(highlight_bounding_box->min.x, 0.0f);
  EXPECT_EQ(highlight_bounding_box->min.y, 0.0f);
  EXPECT_EQ(highlight_bounding_box->min.z, 0.0f);
  EXPECT_EQ(highlight_bounding_box->max.x, 1.0f);
  EXPECT_EQ(highlight_bounding_box->max.y, 2.0f);
  EXPECT_EQ(highlight_bounding_box->max.z, 3.0f);

  // CLear highlights.
  view_wrapper_->ClearHighlights();

  // Verify that DetachViewContents() was called.
  const auto& updated_highlight_bounding_box = annotation_view_->GetCurrentHighlight();
  EXPECT_FALSE(updated_highlight_bounding_box.has_value());
}

TEST_F(ViewWrapperTest, HighlightWithTransform) {
  std::vector<a11y::SemanticTree::TreeUpdate> node_updates;

  // Create test nodes.
  fuchsia::ui::gfx::BoundingBox root_bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                     .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto root_node = CreateTestNode(0u, "test_label_0", {1u});
  root_node.set_location(std::move(root_bounding_box));
  node_updates.emplace_back(std::move(root_node));

  fuchsia::ui::gfx::BoundingBox parent_bounding_box = {.min = {.x = 1.0, .y = 2.0, .z = 3.0},
                                                       .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto parent_node = CreateTestNode(1u, "test_label_1", {2u});
  parent_node.set_transform({2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 1, 1, 1, 1});
  parent_node.set_location(std::move(parent_bounding_box));
  node_updates.emplace_back(std::move(parent_node));

  fuchsia::ui::gfx::BoundingBox child_bounding_box = {.min = {.x = 2.0, .y = 3.0, .z = 4.0},
                                                      .max = {.x = 4.0, .y = 5.0, .z = 6.0}};
  auto child_node = CreateTestNode(2u, "test_label_2", {});
  child_node.set_transform({5, 0, 0, 0, 0, 5, 0, 0, 0, 0, 5, 0, 10, 20, 30, 1});
  child_node.set_location(std::move(child_bounding_box));
  node_updates.emplace_back(std::move(child_node));

  auto tree_ptr = tree_service_->Get();
  ASSERT_TRUE(tree_ptr);

  ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
  RunLoopUntilIdle();

  // Highlight node 1.
  view_wrapper_->HighlightNode(2u);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& highlight_bounding_box = annotation_view_->GetCurrentHighlight();
  EXPECT_TRUE(highlight_bounding_box.has_value());
  EXPECT_EQ(highlight_bounding_box->min.x, 2.0f);
  EXPECT_EQ(highlight_bounding_box->min.y, 3.0f);
  EXPECT_EQ(highlight_bounding_box->min.z, 4.0f);
  EXPECT_EQ(highlight_bounding_box->max.x, 4.0f);
  EXPECT_EQ(highlight_bounding_box->max.y, 5.0f);
  EXPECT_EQ(highlight_bounding_box->max.z, 6.0f);

  const auto& highlight_translation = annotation_view_->GetTranslationVector();
  EXPECT_TRUE(highlight_translation.has_value());
  EXPECT_EQ((*highlight_translation)[0], 15.0f);
  EXPECT_EQ((*highlight_translation)[1], 25.0f);
  EXPECT_EQ((*highlight_translation)[2], 35.0f);

  const auto& highlight_scale = annotation_view_->GetScaleVector();
  EXPECT_TRUE(highlight_scale.has_value());
  EXPECT_EQ((*highlight_scale)[0], 10.0f);
  EXPECT_EQ((*highlight_scale)[1], 15.0f);
  EXPECT_EQ((*highlight_scale)[2], 20.0f);

  // Clear highlights.
  view_wrapper_->ClearHighlights();

  // Verify that DetachViewContents() was called.
  const auto& updated_highlight_bounding_box = annotation_view_->GetCurrentHighlight();
  EXPECT_FALSE(updated_highlight_bounding_box.has_value());
}

}  // namespace
}  // namespace accessibility_test
