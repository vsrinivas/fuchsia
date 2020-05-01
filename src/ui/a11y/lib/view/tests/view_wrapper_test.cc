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
#include <lib/vfs/cpp/pseudo_dir.h>
#include <lib/zx/event.h>

#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "src/lib/syslog/cpp/logger.h"
#include "src/ui/a11y/lib/annotation/annotation_view.h"
#include "src/ui/a11y/lib/annotation/tests/mocks/mock_annotation_view.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_listener.h"
#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace accessibility_test {

class MockSemanticTreeService : public a11y::SemanticTreeService {
  void EnableSemanticsUpdates(bool enabled) { enabled_ = enabled; }

  bool UpdatesEnabled() { return enabled_; }

 private:
  bool enabled_ = false;
};

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
        context_provider_.context()->outgoing()->debug_dir(), [] {});
    tree_service_ = tree_service.get();

    auto mock_annotation_view_factory = std::make_unique<MockAnnotationViewFactory>();
    mock_annotation_view_factory_ = mock_annotation_view_factory.get();

    view_wrapper_ = std::make_unique<a11y::ViewWrapper>(
        std::move(view_ref_), std::move(tree_service), tree_ptr_.NewRequest(), nullptr /*context*/,
        std::move(mock_annotation_view_factory));

    view_wrapper_->EnableSemanticUpdates(true);

    // Verify that semantics are enabled.
    EXPECT_TRUE(semantic_tree_service_factory_->service());
    EXPECT_TRUE(semantic_tree_service_factory_->service()->UpdatesEnabled());

    // Verify that annotation view is initialized.
    auto annotation_view_ptr = mock_annotation_view_factory_->GetAnnotationView(koid_);
    ASSERT_TRUE(annotation_view_ptr);
    EXPECT_TRUE(annotation_view_ptr->IsInitialized());
  }

  vfs::PseudoDir* debug_dir() { return context_provider_.context()->outgoing()->debug_dir(); }

  void CreateTestNode(uint32_t node_id, fuchsia::ui::gfx::BoundingBox bounding_box) {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(node_id);
    node.set_location(std::move(bounding_box));

    std::vector<a11y::SemanticTree::TreeUpdate> node_updates;
    node_updates.emplace_back(std::move(node));

    auto tree_ptr = tree_service_->Get();
    ASSERT_TRUE(tree_ptr);

    ASSERT_TRUE(tree_ptr->Update(std::move(node_updates)));
    RunLoopUntilIdle();
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSemanticTreeServiceFactory> semantic_tree_service_factory_;
  std::unique_ptr<MockSemanticListener> mock_semantic_listener_;
  std::unique_ptr<fidl::Binding<fuchsia::accessibility::semantics::SemanticListener>>
      semantic_listener_binding_;
  std::unique_ptr<a11y::ViewWrapper> view_wrapper_;
  MockAnnotationViewFactory* mock_annotation_view_factory_;
  a11y::SemanticTreeService* tree_service_;
  fuchsia::accessibility::semantics::SemanticTreePtr tree_ptr_;
  fuchsia::ui::views::ViewRef view_ref_;
  zx_koid_t koid_;
};

TEST_F(ViewWrapperTest, HighlightAndClear) {
  // Create test node.
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  // Highlight node 0.
  view_wrapper_->HighlightNode(0u);

  auto annotation_view_ptr = mock_annotation_view_factory_->GetAnnotationView(koid_);
  ASSERT_TRUE(annotation_view_ptr);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
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
  const auto& updated_highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
  EXPECT_FALSE(updated_highlight_bounding_box.has_value());
}

TEST_F(ViewWrapperTest, ViewPropretiesChanged) {
  // Create test node.
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  // Highlight node 0.
  view_wrapper_->HighlightNode(0u);

  auto annotation_view_ptr = mock_annotation_view_factory_->GetAnnotationView(koid_);
  ASSERT_TRUE(annotation_view_ptr);

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
  EXPECT_TRUE(highlight_bounding_box.has_value());
  EXPECT_EQ(highlight_bounding_box->min.x, 0.0f);
  EXPECT_EQ(highlight_bounding_box->min.y, 0.0f);
  EXPECT_EQ(highlight_bounding_box->min.z, 0.0f);
  EXPECT_EQ(highlight_bounding_box->max.x, 1.0f);
  EXPECT_EQ(highlight_bounding_box->max.y, 2.0f);
  EXPECT_EQ(highlight_bounding_box->max.z, 3.0f);

  // Update bounding box of node 0.
  bounding_box = {.min = {.x = 1.0, .y = 1.0, .z = 1.0}, .max = {.x = 2.0, .y = 3.0, .z = 4.0}};
  CreateTestNode(0u, std::move(bounding_box));

  // Simulate view property change event.
  annotation_view_ptr->SimulateViewPropertyChange();

  // Verify that annotation view received updated bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& updated_highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
  EXPECT_TRUE(updated_highlight_bounding_box.has_value());
  EXPECT_EQ(updated_highlight_bounding_box->min.x, 1.0f);
  EXPECT_EQ(updated_highlight_bounding_box->min.y, 1.0f);
  EXPECT_EQ(updated_highlight_bounding_box->min.z, 1.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.x, 2.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.y, 3.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.z, 4.0f);
}

TEST_F(ViewWrapperTest, ViewDetachedAndAttached) {
  // Create test node.
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  // Highlight node 0.
  view_wrapper_->HighlightNode(0u);

  auto annotation_view_ptr = mock_annotation_view_factory_->GetAnnotationView(koid_);
  ASSERT_TRUE(annotation_view_ptr);

  // Simulate view detachment event.
  annotation_view_ptr->SimulateViewDetachment();

  // Verify that DetachViewContents() was called.
  const auto& highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
  EXPECT_FALSE(highlight_bounding_box.has_value());

  // Simulate view re-attachment event.
  annotation_view_ptr->SimulateViewAttachment();

  // Verify that annotation view received bounding_box (defined above) as parameter to
  // DrawHighlight().
  const auto& updated_highlight_bounding_box = annotation_view_ptr->GetCurrentHighlight();
  EXPECT_TRUE(updated_highlight_bounding_box.has_value());
  EXPECT_EQ(updated_highlight_bounding_box->min.x, 0.0f);
  EXPECT_EQ(updated_highlight_bounding_box->min.y, 0.0f);
  EXPECT_EQ(updated_highlight_bounding_box->min.z, 0.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.x, 1.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.y, 2.0f);
  EXPECT_EQ(updated_highlight_bounding_box->max.z, 3.0f);
}

}  // namespace accessibility_test
