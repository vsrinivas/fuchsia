// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/annotation/annotation_view.h"

#include <fuchsia/ui/annotation/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/testing/component_context_provider.h>
#include <lib/sys/cpp/testing/fake_component.h>

#include <set>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/util/util.h"
#include "src/ui/a11y/lib/view/tests/mocks/scenic_mocks.h"

namespace accessibility_test {

class MockAnnotationRegistry : public fuchsia::ui::annotation::Registry {
 public:
  MockAnnotationRegistry() = default;
  ~MockAnnotationRegistry() override = default;

  void CreateAnnotationViewHolder(
      fuchsia::ui::views::ViewRef client_view,
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fuchsia::ui::annotation::Registry::CreateAnnotationViewHolderCallback callback) override {
    create_annotation_view_holder_called_ = true;
    callback();
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::annotation::Registry> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::annotation::Registry> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  bool create_annotation_view_holder_called() { return create_annotation_view_holder_called_; }

 private:
  fidl::BindingSet<fuchsia::ui::annotation::Registry> bindings_;
  bool create_annotation_view_holder_called_;
};

class AnnotationViewTest : public gtest::TestLoopFixture {
 public:
  AnnotationViewTest() = default;
  ~AnnotationViewTest() override = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    auto mock_session = std::make_unique<MockSession>();
    mock_session_ = mock_session.get();
    mock_scenic_ = std::make_unique<MockScenic>(std::move(mock_session));
    mock_annotation_registry_ = std::make_unique<MockAnnotationRegistry>();

    context_provider_.service_directory_provider()->AddService(mock_scenic_->GetHandler());
    context_provider_.service_directory_provider()->AddService(
        mock_annotation_registry_->GetHandler());

    properties_changed_ = false;
    view_attached_ = false;
    view_detached_ = false;

    annotation_view_factory_ = std::make_unique<a11y::AnnotationViewFactory>();

    annotation_view_ = annotation_view_factory_->CreateAndInitAnnotationView(
        CreateOrphanViewRef(), context_provider_.context(),
        [this]() { properties_changed_ = true; }, [this]() { view_attached_ = true; },
        [this]() { view_detached_ = true; });

    RunLoopUntilIdle();
  }

  fuchsia::ui::views::ViewRef CreateOrphanViewRef() {
    fuchsia::ui::views::ViewRef view_ref;

    zx::eventpair::create(0u, &view_ref.reference, &eventpair_peer_);
    return view_ref;
  }

  void ExpectView(ViewAttributes expected) {
    const auto& views = mock_session_->views();
    EXPECT_EQ(views.at(expected.id), expected);
  }

  void ExpectMaterial(uint32_t expected) {
    const auto& materials = mock_session_->materials();
    EXPECT_NE(materials.find(expected), materials.end());
  }

  void ExpectEntityNode(EntityNodeAttributes expected) {
    const auto& entity_nodes = mock_session_->entity_nodes();
    EXPECT_EQ(entity_nodes.at(expected.id), expected);
  }

  void ExpectRectangleNode(RectangleNodeAttributes expected) {
    const auto& rectangle_nodes = mock_session_->rectangle_nodes();
    EXPECT_EQ(rectangle_nodes.at(expected.id), expected);
  }

  void ExpectRectangle(RectangleAttributes expected) {
    const auto& rectangles = mock_session_->rectangles();
    EXPECT_EQ(rectangles.at(expected.id), expected);
  }

  void ExpectHighlightEdge(
      uint32_t id, uint32_t parent_id, float width, float height, float center_x, float center_y,
      float elevation,
      uint32_t content_node_id = a11y::AnnotationView::kFocusHighlightContentNodeId,
      uint32_t material_id = a11y::AnnotationView::kFocusHighlightMaterialId) {
    // Check properties for rectangle shape.
    RectangleAttributes rectangle;
    rectangle.id = id;
    rectangle.parent_id = parent_id;
    rectangle.width = width;
    rectangle.height = height;
    rectangle.center_x = center_x;
    rectangle.center_y = center_y;
    rectangle.elevation = elevation;
    ExpectRectangle(rectangle);

    // Check that rectangle was set as shape of parent node.
    ExpectRectangleNode({parent_id, content_node_id, id, material_id});
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  MockSession* mock_session_;
  std::unique_ptr<MockScenic> mock_scenic_;
  std::unique_ptr<MockAnnotationRegistry> mock_annotation_registry_;
  zx::eventpair eventpair_peer_;
  std::unique_ptr<a11y::AnnotationViewFactory> annotation_view_factory_;
  std::unique_ptr<a11y::AnnotationViewInterface> annotation_view_;
  bool properties_changed_;
  bool view_attached_;
  bool view_detached_;
};

TEST_F(AnnotationViewTest, TestInit) {
  EXPECT_TRUE(mock_annotation_registry_->create_annotation_view_holder_called());

  // Verify that annotation view was created.
  ExpectView({a11y::AnnotationView::kAnnotationViewId, /* view_ref = */ {}, /* children = */ {}});

  // Verify that top-level content node (used to attach/detach annotations from view) was created.
  ExpectEntityNode({a11y::AnnotationView::kFocusHighlightContentNodeId,
                    0u,
                    {}, /* scale vector */
                    {}, /* translation vector */
                    {a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId}});

  // Verify that drawing material was created.
  ExpectMaterial(a11y::AnnotationView::kFocusHighlightMaterialId);

  // Verify that four shape nodes that will hold respective edge rectangles are created and added as
  // children of top-level content node. Also verify material of each.
  ExpectRectangleNode({a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                       a11y::AnnotationView::kFocusHighlightContentNodeId, 0,
                       a11y::AnnotationView::kFocusHighlightMaterialId});
  ExpectRectangleNode({a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                       a11y::AnnotationView::kFocusHighlightContentNodeId, 0,
                       a11y::AnnotationView::kFocusHighlightMaterialId});
  ExpectRectangleNode({a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                       a11y::AnnotationView::kFocusHighlightContentNodeId, 0,
                       a11y::AnnotationView::kFocusHighlightMaterialId});
  ExpectRectangleNode({a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId,
                       a11y::AnnotationView::kFocusHighlightContentNodeId, 0,
                       a11y::AnnotationView::kFocusHighlightMaterialId});
}

TEST_F(AnnotationViewTest, TestDrawFocusHighlight) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);

  RunLoopUntilIdle();

  // Verify that all four expected edges are present.
  // Resource IDs 1-7 are used for the resources created in InitializeView(), so the next available
  // id is 8. Since resource ids are generated incrementally, we expect the four edge rectangles to
  // have ids 8-11.

  // Before we set up the parent View bounding box, the z value of default
  // bounding box is 0.
  constexpr float kHighlightElevation = 0.0f;

  ExpectHighlightEdge(14u, a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.min.x, (bounding_box.min.y + bounding_box.max.y) / 2,
                      kHighlightElevation);

  ExpectHighlightEdge(15u, a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.x, (bounding_box.min.y + bounding_box.max.y) / 2.f,
                      kHighlightElevation);

  ExpectHighlightEdge(16u, a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      kHighlightElevation);

  ExpectHighlightEdge(17u, a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      kHighlightElevation);

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kFocusHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestDrawFocusHighlightAndClearMagnificationHighlight) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);

  RunLoopUntilIdle();

  // This operation should not affect the focus highlight.
  annotation_view_->ClearMagnificationHighlights();

  RunLoopUntilIdle();

  // Verify that all four expected edges are present.
  // Resource IDs 1-7 are used for the resources created in InitializeView(), so the next available
  // id is 8. Since resource ids are generated incrementally, we expect the four edge rectangles to
  // have ids 8-11.

  // Before we set up the parent View bounding box, the z value of default
  // bounding box is 0.
  constexpr float kHighlightElevation = 0.0f;

  ExpectHighlightEdge(14u, a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.min.x, (bounding_box.min.y + bounding_box.max.y) / 2,
                      kHighlightElevation);

  ExpectHighlightEdge(15u, a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.x, (bounding_box.min.y + bounding_box.max.y) / 2.f,
                      kHighlightElevation);

  ExpectHighlightEdge(16u, a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      kHighlightElevation);

  ExpectHighlightEdge(17u, a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      kHighlightElevation);

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kFocusHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestDrawMagnificationHighlight) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, true);

  RunLoopUntilIdle();

  // Verify that all four expected edges are present.
  // Resource IDs 1-7 are used for the resources created in InitializeView(), so the next available
  // id is 8. Since resource ids are generated incrementally, we expect the four edge rectangles to
  // have ids 8-11.

  // Before we set up the parent View bounding box, the z value of default
  // bounding box is 0.
  constexpr float kHighlightElevation = 0.0f;

  ExpectHighlightEdge(14u, a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.min.x, (bounding_box.min.y + bounding_box.max.y) / 2,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(15u, a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.x, (bounding_box.min.y + bounding_box.max.y) / 2.f,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(16u, a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(17u, a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestDrawMagnificationHighlightAndClearFocusHighlight) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, true);

  RunLoopUntilIdle();

  // Attempt to clear focus highlight. This operation should not affect the
  // magnification highlight.
  annotation_view_->ClearFocusHighlights();

  RunLoopUntilIdle();

  // Verify that all four expected edges are present.
  // Resource IDs 1-7 are used for the resources created in InitializeView(), so the next available
  // id is 8. Since resource ids are generated incrementally, we expect the four edge rectangles to
  // have ids 8-11.

  // Before we set up the parent View bounding box, the z value of default
  // bounding box is 0.
  constexpr float kHighlightElevation = 0.0f;

  ExpectHighlightEdge(14u, a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.min.x, (bounding_box.min.y + bounding_box.max.y) / 2,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(15u, a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.y + a11y::AnnotationView::kHighlightEdgeThickness,
                      bounding_box.max.x, (bounding_box.min.y + bounding_box.max.y) / 2.f,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(16u, a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  ExpectHighlightEdge(17u, a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId,
                      bounding_box.max.x + a11y::AnnotationView::kHighlightEdgeThickness,
                      a11y::AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      kHighlightElevation,
                      a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                      a11y::AnnotationView::kMagnificationHighlightMaterialId);

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestClearFocusHighlights) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kFocusHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId}});

  annotation_view_->ClearFocusHighlights();

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was detached
  // from view.
  ExpectEntityNode({a11y::AnnotationView::kFocusHighlightContentNodeId,
                    0u,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kFocusHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kFocusHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestClearMagnificationHighlights) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, true);

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode({a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                    a11y::AnnotationView::kAnnotationViewId,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId}});

  annotation_view_->ClearMagnificationHighlights();

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was detached
  // from view.
  ExpectEntityNode({a11y::AnnotationView::kMagnificationHighlightContentNodeId,
                    0u,
                    {1, 1, 1}, /* scale vector */
                    {0, 0, 0}, /* translation vector */
                    {a11y::AnnotationView::kMagnificationHighlightLeftEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightRightEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightTopEdgeNodeId,
                     a11y::AnnotationView::kMagnificationHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestViewPropertiesChangedEvent) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};

  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);

  RunLoopUntilIdle();

  // Update test node bounding box to reflect change in view properties.
  bounding_box = {.min = {.x = 0, .y = 0, .z = 0}, .max = {.x = 2.0, .y = 4.0, .z = 6.0}};

  mock_session_->SendViewPropertiesChangedEvent(1u /* view id */,
                                                MockSession::kDefaultViewProperties);
  RunLoopUntilIdle();

  EXPECT_TRUE(properties_changed_);
}

TEST_F(AnnotationViewTest, TestViewPropertiesChangedElevation) {
  mock_session_->SendViewPropertiesChangedEvent(1u /* view id */,
                                                MockSession::kDefaultViewProperties);
  RunLoopUntilIdle();

  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);
  RunLoopUntilIdle();

  // Same as the value defined in annotation_view.cc.
  const float kEpsilon = 0.950f;
  const float kExpectedElevation =
      MockSession::kDefaultViewProperties.bounding_box.min.z * kEpsilon;

  const auto& rectangles = mock_session_->rectangles();
  EXPECT_FLOAT_EQ(rectangles.at(14u).elevation, kExpectedElevation);
  EXPECT_FLOAT_EQ(rectangles.at(15u).elevation, kExpectedElevation);
  EXPECT_FLOAT_EQ(rectangles.at(16u).elevation, kExpectedElevation);
  EXPECT_FLOAT_EQ(rectangles.at(17u).elevation, kExpectedElevation);

  EXPECT_TRUE(properties_changed_);
}

TEST_F(AnnotationViewTest, TestViewDetachAndReattachEvents) {
  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  annotation_view_->DrawHighlight(bounding_box, {1, 1, 1}, {0, 0, 0}, false);

  // ViewAttachedToSceneEvent() should have no effect before any highlights are drawn.
  mock_session_->SendViewDetachedFromSceneEvent(1u /* view id */);
  RunLoopUntilIdle();

  EXPECT_TRUE(view_detached_);

  mock_session_->SendViewAttachedToSceneEvent(1u /* view id */);
  RunLoopUntilIdle();

  EXPECT_TRUE(view_attached_);
}

}  // namespace accessibility_test
