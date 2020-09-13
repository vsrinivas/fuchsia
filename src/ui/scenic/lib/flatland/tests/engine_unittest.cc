// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async-testing/test_loop.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <limits>
#include <thread>

#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/lib/display/display_manager.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/flatland/tests/mock_flatland_presenter.h"
#include "src/ui/scenic/lib/flatland/tests/mock_renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

#include <glm/gtx/matrix_transform_2d.hpp>

using ::testing::_;
using ::testing::Return;

using flatland::FlatlandPresenter;
using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::MockFlatlandPresenter;
using flatland::MockRenderer;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::scenic::internal::ContentLink;
using fuchsia::ui::scenic::internal::ContentLinkStatus;
using fuchsia::ui::scenic::internal::ContentLinkToken;
using fuchsia::ui::scenic::internal::GraphLink;
using fuchsia::ui::scenic::internal::GraphLinkToken;
using fuchsia::ui::scenic::internal::LayoutInfo;
using fuchsia::ui::scenic::internal::LinkProperties;

namespace {

class EngineTest : public gtest::RealLoopFixture {
 public:
  EngineTest()
      : uber_struct_system_(std::make_shared<UberStructSystem>()),
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())),
        renderer_(std::make_shared<flatland::NullRenderer>()),
        display_controller_objs_(scenic_impl::display::test::CreateMockDisplayController()) {}

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());

    async_set_default_dispatcher(dispatcher());
    display_manager_ = std::make_unique<scenic_impl::display::DisplayManager>();

    // TODO(fxbug.dev/59646): We want all of the flatland tests to be "headless" and not make use
    // of the real display controller. This isn't fully possible at the moment since we need the
    // real DC's functionality to register buffer collections. Once the new hardware independent
    // display controller driver is ready, we can hook that up to the fidl interface pointer instead
    // and keep the tests hardware agnostic.
    display_manager_->WaitForDefaultDisplayController([] {});
    zx::duration timeout = zx::sec(5);
    RunLoopWithTimeoutOrUntil([this] { return display_manager_->default_display() != nullptr; },
                              timeout);
  }

  void TearDown() override {
    display_manager_.reset();
    sysmem_allocator_ = nullptr;
    gtest::RealLoopFixture::TearDown();
  }

  class FakeFlatlandSession {
   public:
    FakeFlatlandSession(const std::shared_ptr<UberStructSystem>& uber_struct_system,
                        const std::shared_ptr<LinkSystem>& link_system, EngineTest* harness)
        : uber_struct_system_(uber_struct_system),
          link_system_(link_system),
          harness_(harness),
          id_(uber_struct_system_->GetNextInstanceId()),
          graph_(id_),
          queue_(uber_struct_system_->AllocateQueueForSession(id_)) {}

    // Use the TransformGraph API to create and manage transforms and their children.
    TransformGraph& graph() { return graph_; }

    // Returns the link_origin for this session.
    TransformHandle GetLinkOrigin() {
      EXPECT_TRUE(parent_link_.has_value());
      return parent_link_.value().parent_link.link_origin;
    }

    // Clears the ParentLink for this session, if one exists.
    void ClearParentLink() { parent_link_.reset(); }

    // Holds the ContentLink and LinkSystem::ChildLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. Tests should add |child_link.link_handle| to their
    // TransformGraphs to use the ChildLink in a topology.
    struct ChildLink {
      fidl::InterfacePtr<ContentLink> content_link;
      LinkSystem::ChildLink child_link;

      // Returns the handle the parent should add as a child in its local topology to include the
      // link in the topology.
      TransformHandle GetLinkHandle() const { return child_link.link_handle; }
    };

    // Links this session to |parent_session| and returns the ChildLink, which should be used with
    // the parent session. If the return value drops out of scope, tests should call
    // ClearParentLink() on this session.
    ChildLink LinkToParent(FakeFlatlandSession& parent_session) {
      // Create the tokens.
      ContentLinkToken parent_token;
      GraphLinkToken child_token;
      EXPECT_EQ(zx::eventpair::create(0, &parent_token.value, &child_token.value), ZX_OK);

      // Create the parent link.
      fidl::InterfacePtr<GraphLink> graph_link;
      LinkSystem::ParentLink parent_link = link_system_->CreateParentLink(
          std::move(child_token), graph_link.NewRequest(), graph_.CreateTransform());

      // Create the child link.
      fidl::InterfacePtr<ContentLink> content_link;
      LinkSystem::ChildLink child_link = link_system_->CreateChildLink(
          std::move(parent_token), LinkProperties(), content_link.NewRequest(),
          parent_session.graph_.CreateTransform());

      // Run the loop to establish the link.
      harness_->RunLoopUntilIdle();

      parent_link_ = ParentLink({
          .graph_link = std::move(graph_link),
          .parent_link = std::move(parent_link),
      });

      return ChildLink({
          .content_link = std::move(content_link),
          .child_link = std::move(child_link),
      });
    }

    // Allocates a new UberStruct with a local_topology rooted at |local_root|. If this session has
    // a ParentLink, the link_origin of that ParentLink will be used instead.
    std::unique_ptr<UberStruct> CreateUberStructWithCurrentTopology(TransformHandle local_root) {
      auto uber_struct = std::make_unique<UberStruct>();

      // Only use the supplied |local_root| if no there is no ParentLink, otherwise use the
      // |link_origin| from the ParentLink.
      const TransformHandle root =
          parent_link_.has_value() ? parent_link_.value().parent_link.link_origin : local_root;

      // Compute the local topology and place it in the UberStruct.
      auto local_topology_data =
          graph_.ComputeAndCleanup(root, std::numeric_limits<uint64_t>::max());
      EXPECT_NE(local_topology_data.iterations, std::numeric_limits<uint64_t>::max());
      EXPECT_TRUE(local_topology_data.cyclical_edges.empty());

      uber_struct->local_topology = local_topology_data.sorted_transforms;

      return uber_struct;
    }

    // Pushes |uber_struct| to the UberStructSystem and updates the system so that it represents
    // this session in the InstanceMap.
    void PushUberStruct(std::unique_ptr<UberStruct> uber_struct) {
      EXPECT_FALSE(uber_struct->local_topology.empty());
      EXPECT_EQ(uber_struct->local_topology[0].handle.GetInstanceId(), id_);

      queue_->Push(/*present_id=*/0, std::move(uber_struct));
      uber_struct_system_->UpdateSessions({{id_, 0}});
    }

   private:
    // Shared systems for all sessions.
    std::shared_ptr<UberStructSystem> uber_struct_system_;
    std::shared_ptr<LinkSystem> link_system_;

    // The test harness to give access to RunLoopUntilIdle().
    EngineTest* harness_;

    // Data specific this session.
    scheduling::SessionId id_;
    TransformGraph graph_;
    std::shared_ptr<UberStructSystem::UberStructQueue> queue_;

    // Holds the GraphLink and LinkSystem::ParentLink objects since if they fall out of scope,
    // the LinkSystem will delete the link. When |parent_link_| has a value, the
    // |parent_link.link_origin| from this object is used as the root TransformHandle.
    struct ParentLink {
      fidl::InterfacePtr<GraphLink> graph_link;
      LinkSystem::ParentLink parent_link;
    };
    std::optional<ParentLink> parent_link_;
  };

  FakeFlatlandSession CreateSession() {
    return FakeFlatlandSession(uber_struct_system_, link_system_, this);
  }

 protected:
  // Systems that are populated with data from Flatland instances.
  const std::shared_ptr<UberStructSystem> uber_struct_system_;
  const std::shared_ptr<LinkSystem> link_system_;
  std::shared_ptr<flatland::NullRenderer> renderer_;
  const scenic_impl::display::test::DisplayControllerObjects display_controller_objs_;
  std::unique_ptr<scenic_impl::display::DisplayManager> display_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

}  // namespace

namespace flatland {
namespace test {

// Test bad input to the engine |RegisterTargetCollectionFunction|.
TEST_F(EngineTest, BadBufferRegistration) {
  auto display_controller = display_manager_->default_display_controller();
  if (!display_controller) {
    return;
  }

  auto display = display_manager_->default_display();
  if (!display) {
    return;
  }

  Engine engine(display_controller, renderer_, link_system_, uber_struct_system_);

  const uint32_t kDisplayId = display->display_id();
  const uint32_t kWidth = display->width_in_px();
  const uint32_t kHeight = display->height_in_px();
  const uint32_t kNumVmos = 2;

  // Try to register a buffer collection without first adding a display.
  auto [renderer_id, display_id] =
      engine.RegisterTargetCollection(sysmem_allocator_.get(), kDisplayId, kNumVmos);
  EXPECT_EQ(renderer_id, Renderer::kInvalidId);
  EXPECT_EQ(display_id, 0u);

  // Now add the display.
  engine.AddDisplay(kDisplayId, TransformHandle(), glm::uvec2(kWidth, kHeight));

  // Try again with 0 vmos. This should also fail.
  auto [renderer_id_2, display_id_2] =
      engine.RegisterTargetCollection(sysmem_allocator_.get(), kDisplayId, /*num_vmos*/ 0);
  EXPECT_EQ(renderer_id_2, Renderer::kInvalidId);
  EXPECT_EQ(display_id_2, 0u);

  // Now use a positive vmo number, this should work.
  auto [renderer_id_3, display_id_3] =
      engine.RegisterTargetCollection(sysmem_allocator_.get(), kDisplayId, kNumVmos);
  EXPECT_NE(renderer_id_3, Renderer::kInvalidId);
  EXPECT_NE(display_id_3, 0u);
}

// Test to make sure we can register framebuffers to the renderer and display
// via the engine. Requires the use of the real display controller.
TEST_F(EngineTest, BufferRegistrationTest) {
  auto display_controller = display_manager_->default_display_controller();
  if (!display_controller) {
    return;
  }

  auto display = display_manager_->default_display();
  ASSERT_TRUE(display_controller);
  if (!display) {
    return;
  }

  const uint32_t kDisplayId = display->display_id();
  const uint32_t kWidth = display->width_in_px();
  const uint32_t kHeight = display->height_in_px();
  const uint32_t kNumVmos = 2;

  Engine engine(display_controller, renderer_, link_system_, uber_struct_system_);
  engine.AddDisplay(kDisplayId, TransformHandle(), glm::uvec2(kWidth, kHeight));
  auto [renderer_id, display_id] =
      engine.RegisterTargetCollection(sysmem_allocator_.get(), kDisplayId, kNumVmos);
  EXPECT_NE(renderer_id, Renderer::kInvalidId);
  EXPECT_NE(display_id, 0u);

  // We can check the result of buffer registration by the engine through the renderer.
  // We should see the same number of vmos we told the engine to create, as well as each
  // vmo being the same width and height in pixels as the display.
  auto result = renderer_->Validate(renderer_id);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(result->vmo_count, kNumVmos);
  EXPECT_EQ(result->image_constraints.required_min_coded_width, kWidth);
  EXPECT_EQ(result->image_constraints.required_min_coded_height, kHeight);
}

// When compositing directly to a hardware display layer, the display controller
// takes in source and destination Frame object types, which mirrors flatland usage.
// The source frames are nonnormalized UV coordinates and the destination frames are
// screenspace coordinates given in pixels. So this test makes sure that the rectangle
// and frame data that is generated by flatland sends along to the display controller
// the proper source and destination frame data. Each source and destination frame pair
// should be added to its own layer on the display.
TEST_F(EngineTest, HardwareFrameCorrectnessTest) {
  // Create a parent and child session.
  auto parent_session = CreateSession();
  auto child_session = CreateSession();

  // Create a link between the two.
  auto child_link = child_session.LinkToParent(parent_session);

  // Create the root handle for the parent and a handle that will have an image attached.
  const TransformHandle parent_root_handle = parent_session.graph().CreateTransform();
  const TransformHandle parent_image_handle = parent_session.graph().CreateTransform();

  // Add the two children to the parent root: link, then image.
  parent_session.graph().AddChild(parent_root_handle, child_link.GetLinkHandle());
  parent_session.graph().AddChild(parent_root_handle, parent_image_handle);

  // Create an image handle for the child.
  const TransformHandle child_image_handle = child_session.graph().CreateTransform();

  // Attach that image handle to the link_origin.
  child_session.graph().AddChild(child_session.GetLinkOrigin(), child_image_handle);

  // Get an UberStruct for the parent session.
  auto parent_struct = parent_session.CreateUberStructWithCurrentTopology(parent_root_handle);

  // Add an image.
  parent_struct->images[parent_image_handle] = ImageMetadata({
      .vmo_idx = 1,
      .width = 128,
      .height = 256,
  });

  parent_struct->local_matrices[parent_image_handle] = glm::mat3(1);
  parent_struct->local_matrices[parent_image_handle] =
      glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(9, 13)), glm::vec2(10, 20));

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  // Get an UberStruct for the child session. Note that the argument will be ignored anyway.
  auto child_struct =
      child_session.CreateUberStructWithCurrentTopology(child_session.GetLinkOrigin());

  // Add an image.
  child_struct->images[child_image_handle] = ImageMetadata({
      .vmo_idx = 2,
      .width = 512,
      .height = 1024,
  });
  child_struct->local_matrices[child_image_handle] =
      glm::scale(glm::translate(glm::mat3(1), glm::vec2(5, 7)), glm::vec2(30, 40));

  // Submit the UberStruct.
  child_session.PushUberStruct(std::move(child_struct));

  auto display_controller = display_controller_objs_.interface_ptr;
  auto& mock = display_controller_objs_.mock;

  uint64_t display_id = 1;
  glm::uvec2 resolution(1024, 768);

  // We will end up with 2 source frames, 2 destination frames, and two layers beind sent to the
  // display.
  bool set_display_layers_called = false;
  uint32_t set_layer_called_count = 0;
  fuchsia::hardware::display::Frame sources[2];
  fuchsia::hardware::display::Frame destinations[2];
  uint64_t layer_ids[2];

  // Set the mock display controller functions and wait for messages.
  std::thread server([&display_id, &set_display_layers_called, &set_layer_called_count, &layer_ids,
                      &sources, &destinations, &mock]() mutable {
    async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

    mock->set_set_display_layers_fn([&](uint64_t in_display_id, ::std::vector<uint64_t> layer_ids) {
      EXPECT_EQ(display_id, in_display_id);
      set_display_layers_called = true;
      EXPECT_EQ(layer_ids[0], 1u);
      EXPECT_EQ(layer_ids[1], 2u);

      // This function should be called before we call the SetLayerPrimaryPosition function.
      EXPECT_EQ(set_layer_called_count, 0u);
    });

    mock->set_layer_primary_position_fn(
        [&](uint64_t layer_id, fuchsia::hardware::display::Transform transform,
            fuchsia::hardware::display::Frame src, fuchsia::hardware::display::Frame dst) {
          layer_ids[set_layer_called_count] = layer_id;
          sources[set_layer_called_count] = src;
          destinations[set_layer_called_count] = dst;
          set_layer_called_count++;
        });

    // Since we have 2 rectangles with images, we have to wait for 2 calls to initialize layers,
    // 1 call to set the layers on the display, and 2 calls to set the layer primary positions.
    // This all happens when we call engine.RenderFrame() below.
    for (uint32_t i = 0; i < 5; i++) {
      mock->WaitForMessage();
    }
  });

  // Create an engine.
  Engine engine(display_controller, renderer_, link_system_, uber_struct_system_);

  engine.AddDisplay(display_id, parent_root_handle, resolution);
  engine.RenderFrame();

  server.join();

  EXPECT_EQ(set_layer_called_count, 2u);
  EXPECT_EQ(layer_ids[0], 1u);
  EXPECT_EQ(layer_ids[1], 2u);

  EXPECT_EQ(sources[0].x_pos, 0u);
  EXPECT_EQ(sources[0].y_pos, 0u);
  EXPECT_EQ(sources[0].width, 512u);
  EXPECT_EQ(sources[0].height, 1024u);

  EXPECT_EQ(destinations[0].x_pos, 5u);
  EXPECT_EQ(destinations[0].y_pos, 7u);
  EXPECT_EQ(destinations[0].width, 30u);
  EXPECT_EQ(destinations[0].height, 40u);

  EXPECT_EQ(sources[1].x_pos, 0u);
  EXPECT_EQ(sources[1].y_pos, 0u);
  EXPECT_EQ(sources[1].width, 128u);
  EXPECT_EQ(sources[1].height, 256u);

  EXPECT_EQ(destinations[1].x_pos, 9u);
  EXPECT_EQ(destinations[1].y_pos, 13u);
  EXPECT_EQ(destinations[1].width, 10u);
  EXPECT_EQ(destinations[1].height, 20u);
}

}  // namespace test
}  // namespace flatland
