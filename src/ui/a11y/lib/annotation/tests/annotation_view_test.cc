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

#include <map>
#include <set>
#include <vector>

#include <gtest/gtest.h>

#include "src/ui/a11y/lib/semantics/tests/mocks/mock_semantic_provider.h"
#include "src/ui/a11y/lib/view/view_manager.h"

namespace a11y {
namespace {

static constexpr fuchsia::ui::gfx::ViewProperties kViewProperties = {
    .bounding_box = {.min = {.x = 10, .y = 5}, .max = {.x = 100, .y = 50}}};

struct ViewAttributes {
  uint32_t id;
  std::set<uint32_t> children;
  bool operator==(const ViewAttributes& rhs) const {
    return this->id == rhs.id && this->children == rhs.children;
  }
};

struct EntityNodeAttributes {
  uint32_t id;
  uint32_t parent_id;
  std::set<uint32_t> children;
  bool operator==(const EntityNodeAttributes& rhs) const {
    return this->id == rhs.id && this->parent_id == rhs.parent_id && this->children == rhs.children;
  }
};

struct RectangleNodeAttributes {
  uint32_t id;
  uint32_t parent_id;
  uint32_t rectangle_id;
  uint32_t material_id;
  bool operator==(const RectangleNodeAttributes& rhs) const {
    return this->id == rhs.id && this->parent_id == rhs.parent_id &&
           this->rectangle_id == rhs.rectangle_id && this->material_id == rhs.material_id;
  }
};

struct RectangleAttributes {
  uint32_t id;
  uint32_t parent_id;
  float width;
  float height;
  float elevation;
  float center_x;
  float center_y;
  bool operator==(const RectangleAttributes& rhs) const {
    return this->id == rhs.id && this->parent_id == rhs.parent_id && this->width == rhs.width &&
           this->height == rhs.height && this->elevation == rhs.elevation &&
           this->center_x == rhs.center_x && this->center_y == rhs.center_y;
  }
};

class MockAnnotationRegistry : public fuchsia::ui::annotation::Registry {
 public:
  MockAnnotationRegistry() = default;
  ~MockAnnotationRegistry() override = default;

  void CreateAnnotationViewHolder(
      fuchsia::ui::views::ViewRef client_view,
      fuchsia::ui::views::ViewHolderToken view_holder_token,
      fuchsia::ui::annotation::Registry::CreateAnnotationViewHolderCallback callback) override {
    create_annotation_view_holder_called_ = true;
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

class MockSession : public fuchsia::ui::scenic::testing::Session_TestBase {
 public:
  MockSession() : binding_(this) {}

  void NotImplemented_(const std::string& name) override {}

  void Enqueue(std::vector<fuchsia::ui::scenic::Command> cmds) override {
    cmd_queue_.insert(cmd_queue_.end(), std::make_move_iterator(cmds.begin()),
                      std::make_move_iterator(cmds.end()));
  }

  void ApplyCreateResourceCommand(const fuchsia::ui::gfx::CreateResourceCmd& command) {
    const uint32_t id = command.id;
    switch (command.resource.Which()) {
      case fuchsia::ui::gfx::ResourceArgs::Tag::kView3:
        views_[id].id = id;
        break;

      case fuchsia::ui::gfx::ResourceArgs::Tag::kEntityNode:
        entity_nodes_[id].id = id;
        break;

      case fuchsia::ui::gfx::ResourceArgs::Tag::kShapeNode:
        rectangle_nodes_[id].id = id;
        break;

      case fuchsia::ui::gfx::ResourceArgs::Tag::kMaterial:
        materials_.emplace(id);
        break;

      case fuchsia::ui::gfx::ResourceArgs::Tag::kRectangle:
        EXPECT_GE(id, 8u);
        rectangles_[id].id = id;
        rectangles_[id].width = command.resource.rectangle().width.vector1();
        rectangles_[id].height = command.resource.rectangle().height.vector1();
        break;

      default:
        break;
    }
  }

  void ApplyAddChildCommand(const fuchsia::ui::gfx::AddChildCmd& command) {
    const uint32_t parent_id = command.node_id;
    const uint32_t child_id = command.child_id;

    // Update parent's children. Only views and entity nodes will have children. Also, resource ids
    // are unique globally across all resource types, so only one of views_ and entity_nodes_ will
    // contain parent_id as a key.
    if (views_.find(parent_id) != views_.end()) {
      views_[parent_id].children.insert(child_id);
    } else if (entity_nodes_.find(parent_id) != entity_nodes_.end()) {
      entity_nodes_[parent_id].children.insert(child_id);
    }

    // Update child's parent. Only entity nodes and shape nodes will have parents.
    if (entity_nodes_.find(child_id) != entity_nodes_.end()) {
      entity_nodes_[child_id].parent_id = parent_id;
    } else if (rectangle_nodes_.find(child_id) != rectangle_nodes_.end()) {
      rectangle_nodes_[child_id].parent_id = parent_id;
    }
  }

  void ApplySetMaterialCommand(const fuchsia::ui::gfx::SetMaterialCmd& command) {
    rectangle_nodes_[command.node_id].material_id = command.material_id;
  }

  void ApplySetShapeCommand(const fuchsia::ui::gfx::SetShapeCmd& command) {
    const uint32_t node_id = command.node_id;
    const uint32_t rectangle_id = command.shape_id;

    rectangle_nodes_[node_id].rectangle_id = rectangle_id;
    rectangles_[rectangle_id].parent_id = node_id;
  }

  void ApplySetTranslationCommand(const fuchsia::ui::gfx::SetTranslationCmd& command) {
    const uint32_t parent_id = command.id;
    const uint32_t rectangle_id = rectangle_nodes_[parent_id].rectangle_id;
    const auto& translation = command.value.value;
    rectangles_[rectangle_id].center_x = translation.x;
    rectangles_[rectangle_id].center_y = translation.y;
    rectangles_[rectangle_id].elevation = translation.z;
  }

  void ApplyDetachCommand(const fuchsia::ui::gfx::DetachCmd& command) {
    const uint32_t id = command.id;

    // The annotation view only ever detaches the content entity node from the view node.
    auto& entity_node = entity_nodes_[id];

    if (entity_node.parent_id != 0) {
      views_[entity_node.parent_id].children.erase(id);
    }

    entity_node.parent_id = 0u;
  }

  void Present(uint64_t presentation_time, ::std::vector<::zx::event> acquire_fences,
               ::std::vector<::zx::event> release_fences, PresentCallback callback) override {
    EXPECT_FALSE(cmd_queue_.empty());

    for (const auto& command : cmd_queue_) {
      if (command.Which() != fuchsia::ui::scenic::Command::Tag::kGfx) {
        continue;
      }

      const auto& gfx_command = command.gfx();

      switch (gfx_command.Which()) {
        case fuchsia::ui::gfx::Command::Tag::kCreateResource:
          ApplyCreateResourceCommand(gfx_command.create_resource());
          break;

        case fuchsia::ui::gfx::Command::Tag::kAddChild:
          ApplyAddChildCommand(gfx_command.add_child());
          break;

        case fuchsia::ui::gfx::Command::Tag::kSetMaterial:
          ApplySetMaterialCommand(gfx_command.set_material());
          break;

        case fuchsia::ui::gfx::Command::Tag::kSetShape:
          ApplySetShapeCommand(gfx_command.set_shape());
          break;

        case fuchsia::ui::gfx::Command::Tag::kSetTranslation:
          ApplySetTranslationCommand(gfx_command.set_translation());
          break;

        case fuchsia::ui::gfx::Command::Tag::kDetach:
          ApplyDetachCommand(gfx_command.detach());
          break;

        default:
          break;
      }
    }
  }

  void SendGfxEvent(fuchsia::ui::gfx::Event event) {
    fuchsia::ui::scenic::Event scenic_event;
    scenic_event.set_gfx(std::move(event));

    std::vector<fuchsia::ui::scenic::Event> events;
    events.emplace_back(std::move(scenic_event));

    listener_->OnScenicEvent(std::move(events));
  }

  void SendViewPropertiesChangedEvent() {
    fuchsia::ui::gfx::ViewPropertiesChangedEvent view_properties_changed_event = {
        .view_id = 1u,
        .properties = kViewProperties,
    };
    fuchsia::ui::gfx::Event event;
    event.set_view_properties_changed(view_properties_changed_event);

    SendGfxEvent(std::move(event));
  }

  void SendViewDetachedFromSceneEvent() {
    fuchsia::ui::gfx::ViewDetachedFromSceneEvent view_detached_from_scene_event = {.view_id = 1u};
    fuchsia::ui::gfx::Event event;
    event.set_view_detached_from_scene(view_detached_from_scene_event);

    SendGfxEvent(std::move(event));
  }

  void SendViewAttachedToSceneEvent() {
    fuchsia::ui::gfx::ViewAttachedToSceneEvent view_attached_to_scene_event = {.view_id = 1u};
    fuchsia::ui::gfx::Event event;
    event.set_view_attached_to_scene(view_attached_to_scene_event);

    SendGfxEvent(std::move(event));
  }

  void Bind(fidl::InterfaceRequest<::fuchsia::ui::scenic::Session> request,
            ::fuchsia::ui::scenic::SessionListenerPtr listener) {
    binding_.Bind(std::move(request));
    listener_ = std::move(listener);
  }

  const std::set<uint32_t>& materials() { return materials_; }
  const std::unordered_map<uint32_t, ViewAttributes>& views() { return views_; }
  const std::unordered_map<uint32_t, EntityNodeAttributes>& entity_nodes() { return entity_nodes_; }
  const std::unordered_map<uint32_t, RectangleNodeAttributes>& rectangle_nodes() {
    return rectangle_nodes_;
  }
  const std::unordered_map<uint32_t, RectangleAttributes>& rectangles() { return rectangles_; }

 private:
  fidl::Binding<fuchsia::ui::scenic::Session> binding_;
  fuchsia::ui::scenic::SessionListenerPtr listener_;
  std::vector<fuchsia::ui::scenic::Command> cmd_queue_;

  std::set<uint32_t> materials_;
  std::unordered_map<uint32_t, ViewAttributes> views_;
  std::unordered_map<uint32_t, EntityNodeAttributes> entity_nodes_;
  std::unordered_map<uint32_t, RectangleNodeAttributes> rectangle_nodes_;
  std::unordered_map<uint32_t, RectangleAttributes> rectangles_;
};

class FakeScenic : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  explicit FakeScenic(MockSession* mock_session) : mock_session_(mock_session) {}

  void NotImplemented_(const std::string& name) override {}

  void CreateSession(
      fidl::InterfaceRequest<fuchsia::ui::scenic::Session> session,
      fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> listener) override {
    mock_session_->Bind(std::move(session), listener.Bind());
    create_session_called_ = true;
  }

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

  bool create_session_called() { return create_session_called_; }

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
  MockSession* mock_session_;
  bool create_session_called_;
};

class AnnotationViewTest : public gtest::TestLoopFixture {
 public:
  AnnotationViewTest() = default;

  void SetUp() override {
    gtest::TestLoopFixture::SetUp();

    mock_session_ = std::make_unique<MockSession>();
    fake_scenic_ = std::make_unique<FakeScenic>(mock_session_.get());
    mock_annotation_registry_ = std::make_unique<MockAnnotationRegistry>();
    view_manager_ =
        std::make_unique<ViewManager>(std::make_unique<a11y::SemanticTreeServiceFactory>(),
                                      context_provider_.context()->outgoing()->debug_dir());
    view_manager_->SetSemanticsEnabled(true);
    RunLoopUntilIdle();

    mock_semantic_provider_ =
        std::make_unique<accessibility_test::MockSemanticProvider>(view_manager_.get());

    RunLoopUntilIdle();

    context_provider_.service_directory_provider()->AddService(fake_scenic_->GetHandler());
    context_provider_.service_directory_provider()->AddService(
        mock_annotation_registry_->GetHandler());

    RunLoopUntilIdle();
  }

  fuchsia::ui::views::ViewRef CreateOrphanViewRef() {
    fuchsia::ui::views::ViewRef view_ref;

    zx::eventpair::create(0u, &view_ref.reference, &eventpair_peer_);
    return view_ref;
  }

  void CreateTestNode(uint32_t node_id, fuchsia::ui::gfx::BoundingBox bounding_box) {
    fuchsia::accessibility::semantics::Node node;
    node.set_node_id(node_id);
    node.set_location(std::move(bounding_box));

    std::vector<fuchsia::accessibility::semantics::Node> node_updates;
    node_updates.push_back(std::move(node));
    mock_semantic_provider_->UpdateSemanticNodes(std::move(node_updates));
    RunLoopUntilIdle();

    mock_semantic_provider_->CommitUpdates();
    RunLoopUntilIdle();
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

  void ExpectHighlightEdge(uint32_t id, uint32_t parent_id, float width, float height,
                           float center_x, float center_y, float elevation) {
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
    ExpectRectangleNode(
        {parent_id, AnnotationView::kContentNodeId, id, AnnotationView::kHighlightMaterialId});
  }

 protected:
  sys::testing::ComponentContextProvider context_provider_;
  std::unique_ptr<MockSession> mock_session_;
  std::unique_ptr<FakeScenic> fake_scenic_;
  std::unique_ptr<MockAnnotationRegistry> mock_annotation_registry_;
  std::unique_ptr<ViewManager> view_manager_;
  std::unique_ptr<accessibility_test::MockSemanticProvider> mock_semantic_provider_;
  zx::eventpair eventpair_peer_;
};

TEST_F(AnnotationViewTest, TestInit) {
  fuchsia::ui::views::ViewRef view_ref;
  fidl::Clone(mock_semantic_provider_->view_ref(), &view_ref);
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 mock_semantic_provider_->koid());
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  EXPECT_TRUE(mock_annotation_registry_->create_annotation_view_holder_called());

  EXPECT_FALSE(mock_semantic_provider_->IsChannelClosed());

  // Verify that annotation view was created.
  ExpectView({AnnotationView::kAnnotationViewId, {}});

  // Verify that top-level content node (used to attach/detach annotations from view) was created.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       0u,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});

  // Verify that drawing material was created.
  ExpectMaterial(AnnotationView::kHighlightMaterialId);

  // Verify that four shape nodes that will hold respective edge rectangles are created and added as
  // children of top-level content node. Also verify material of each.
  ExpectRectangleNode({AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kContentNodeId, 0,
                       AnnotationView::kHighlightMaterialId});
  ExpectRectangleNode({AnnotationView::kHighlightRightEdgeNodeId, AnnotationView::kContentNodeId, 0,
                       AnnotationView::kHighlightMaterialId});
  ExpectRectangleNode({AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kContentNodeId, 0,
                       AnnotationView::kHighlightMaterialId});
  ExpectRectangleNode({AnnotationView::kHighlightBottomEdgeNodeId, AnnotationView::kContentNodeId,
                       0, AnnotationView::kHighlightMaterialId});
}

TEST_F(AnnotationViewTest, TestHighlightNode) {
  fuchsia::ui::views::ViewRef view_ref;
  fidl::Clone(mock_semantic_provider_->view_ref(), &view_ref);
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 mock_semantic_provider_->koid());
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  annotation_view.HighlightNode(0u);

  RunLoopUntilIdle();

  // Verify that all four expected edges are present.
  // Resource IDs 1-7 are used for the resources created in InitializeView(), so the next available
  // id is 8. Since resource ids are generated incrementally, we expect the four edge rectangles to
  // have ids 8-11.
  ExpectHighlightEdge(
      8u, AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightEdgeThickness,
      bounding_box.max.y + AnnotationView::kHighlightEdgeThickness, bounding_box.min.x,
      (bounding_box.min.y + bounding_box.max.y) / 2, bounding_box.max.z);

  ExpectHighlightEdge(
      9u, AnnotationView::kHighlightRightEdgeNodeId, AnnotationView::kHighlightEdgeThickness,
      bounding_box.max.y + AnnotationView::kHighlightEdgeThickness, bounding_box.max.x,
      (bounding_box.min.y + bounding_box.max.y) / 2.f, bounding_box.max.z);

  ExpectHighlightEdge(10u, AnnotationView::kHighlightTopEdgeNodeId,
                      bounding_box.max.x + AnnotationView::kHighlightEdgeThickness,
                      AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      bounding_box.max.z);

  ExpectHighlightEdge(11u, AnnotationView::kHighlightBottomEdgeNodeId,
                      bounding_box.max.x + AnnotationView::kHighlightEdgeThickness,
                      AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      bounding_box.max.z);

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       AnnotationView::kAnnotationViewId,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestDetachViewContents) {
  fuchsia::ui::views::ViewRef view_ref;
  fidl::Clone(mock_semantic_provider_->view_ref(), &view_ref);
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 mock_semantic_provider_->koid());
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  annotation_view.HighlightNode(0u);

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was attached
  // to view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       AnnotationView::kAnnotationViewId,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});

  annotation_view.DetachViewContents();

  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was detached
  // from view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       0u,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});
}

TEST_F(AnnotationViewTest, TestViewPropertiesChangedEvent) {
  fuchsia::ui::views::ViewRef view_ref;
  fidl::Clone(mock_semantic_provider_->view_ref(), &view_ref);
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 mock_semantic_provider_->koid());
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  annotation_view.HighlightNode(0u);

  RunLoopUntilIdle();

  // Update test node bounding box to reflect change in view properties.
  bounding_box = {.min = {.x = 0, .y = 0, .z = 0}, .max = {.x = 2.0, .y = 4.0, .z = 6.0}};
  CreateTestNode(0u, std::move(bounding_box));

  mock_session_->SendViewPropertiesChangedEvent();
  RunLoopUntilIdle();

  ExpectHighlightEdge(
      12u, AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightEdgeThickness,
      bounding_box.max.y + AnnotationView::kHighlightEdgeThickness, bounding_box.min.x,
      (bounding_box.min.y + bounding_box.max.y) / 2, bounding_box.max.z);

  ExpectHighlightEdge(
      13u, AnnotationView::kHighlightRightEdgeNodeId, AnnotationView::kHighlightEdgeThickness,
      bounding_box.max.y + AnnotationView::kHighlightEdgeThickness, bounding_box.max.x,
      (bounding_box.min.y + bounding_box.max.y) / 2.f, bounding_box.max.z);

  ExpectHighlightEdge(14u, AnnotationView::kHighlightTopEdgeNodeId,
                      bounding_box.max.x + AnnotationView::kHighlightEdgeThickness,
                      AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.max.y,
                      bounding_box.max.z);

  ExpectHighlightEdge(15u, AnnotationView::kHighlightBottomEdgeNodeId,
                      bounding_box.max.x + AnnotationView::kHighlightEdgeThickness,
                      AnnotationView::kHighlightEdgeThickness,
                      (bounding_box.min.x + bounding_box.max.x) / 2.f, bounding_box.min.y,
                      bounding_box.max.z);
}

TEST_F(AnnotationViewTest, TestViewDetachAndReattachEvents) {
  fuchsia::ui::views::ViewRef view_ref;
  fidl::Clone(mock_semantic_provider_->view_ref(), &view_ref);
  AnnotationView annotation_view(context_provider_.context(), view_manager_.get(),
                                 mock_semantic_provider_->koid());
  annotation_view.InitializeView(std::move(view_ref));

  RunLoopUntilIdle();

  fuchsia::ui::gfx::BoundingBox bounding_box = {.min = {.x = 0, .y = 0, .z = 0},
                                                .max = {.x = 1.0, .y = 2.0, .z = 3.0}};
  CreateTestNode(0u, std::move(bounding_box));

  // ViewAttachedToSceneEvent() should have no effect before any highlights are drawn.
  mock_session_->SendViewAttachedToSceneEvent();
  RunLoopUntilIdle();

  // Verify that top-level content node remains detached from view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       0u,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});

  annotation_view.HighlightNode(0u);

  RunLoopUntilIdle();

  // Update test node bounding box to reflect change in view properties.
  bounding_box = {.min = {.x = 0, .y = 0, .z = 0}, .max = {.x = 2.0, .y = 4.0, .z = 6.0}};
  CreateTestNode(0u, std::move(bounding_box));

  mock_session_->SendViewDetachedFromSceneEvent();
  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was detached
  // from view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       0u,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});

  mock_session_->SendViewAttachedToSceneEvent();
  RunLoopUntilIdle();

  // Verify that top-level content node (used to attach/detach annotations from view) was
  // re-attached to view.
  ExpectEntityNode(
      {AnnotationView::kContentNodeId,
       AnnotationView::kAnnotationViewId,
       {AnnotationView::kHighlightLeftEdgeNodeId, AnnotationView::kHighlightRightEdgeNodeId,
        AnnotationView::kHighlightTopEdgeNodeId, AnnotationView::kHighlightBottomEdgeNodeId}});
}

}  // namespace
}  // namespace a11y
