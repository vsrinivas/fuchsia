// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/engine/engine.h"

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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/real_loop_fixture.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/engine/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/flatland.h"
#include "src/ui/scenic/lib/flatland/global_image_data.h"
#include "src/ui/scenic/lib/flatland/global_matrix_data.h"
#include "src/ui/scenic/lib/flatland/global_topology_data.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/renderer.h"
#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

#include <glm/gtx/matrix_transform_2d.hpp>

using ::testing::_;
using ::testing::Return;

using flatland::ImageMetadata;
using flatland::LinkSystem;
using flatland::MockDisplayController;
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
        link_system_(std::make_shared<LinkSystem>(uber_struct_system_->GetNextInstanceId())) {}

  void SetUp() override {
    gtest::RealLoopFixture::SetUp();

    // Create the SysmemAllocator.
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());

    async_set_default_dispatcher(dispatcher());

    renderer_ = std::make_shared<flatland::NullRenderer>();

    zx::channel device_channel_server;
    zx::channel device_channel_client;
    FX_CHECK(ZX_OK == zx::channel::create(0, &device_channel_server, &device_channel_client));
    zx::channel controller_channel_server;
    zx::channel controller_channel_client;
    FX_CHECK(ZX_OK ==
             zx::channel::create(0, &controller_channel_server, &controller_channel_client));

    mock_display_controller_ = std::make_unique<flatland::MockDisplayController>();
    mock_display_controller_->Bind(std::move(device_channel_server),
                                   std::move(controller_channel_server));

    auto unique_display_controller =
        std::make_unique<fuchsia::hardware::display::ControllerSyncPtr>();
    unique_display_controller->Bind(std::move(controller_channel_client));

    engine_ = std::make_unique<flatland::Engine>(std::move(unique_display_controller), renderer_,
                                                 link_system_, uber_struct_system_);
  }

  void TearDown() override {
    sysmem_allocator_ = nullptr;
    renderer_.reset();
    engine_.reset();
    mock_display_controller_.reset();

    // Move the channel to a local variable which will go out of scope
    // and close when this function returns.
    auto local = local_.release();

    gtest::RealLoopFixture::TearDown();
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CreateToken() {
    zx::channel remote;
    zx::channel::create(0, &local_, &remote);
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token{std::move(remote)};
    return token;
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
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::unique_ptr<flatland::Engine> engine_;
  std::unique_ptr<flatland::MockDisplayController> mock_display_controller_;

 private:
  zx::channel local_;
};

}  // namespace

namespace flatland {
namespace test {

TEST_F(EngineTest, ImportAndReleaseBufferCollectionTest) {
  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // Wait once for call to ImportBufferCollection, once for setting the
    // constraints, and once for call to ReleaseBufferCollection
    for (uint32_t i = 0; i < 3; i++) {
      mock->WaitForMessage();
    }
  });

  const sysmem_util::GlobalBufferCollectionId kGlobalBufferCollectionId = 15;

  EXPECT_CALL(*mock_display_controller_.get(),
              ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<class ::fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*mock_display_controller_.get(),
              SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  engine_->ImportBufferCollection(kGlobalBufferCollectionId, nullptr, CreateToken());

  EXPECT_CALL(*mock_display_controller_, ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  engine_->ReleaseBufferCollection(kGlobalBufferCollectionId);

  server.join();
}

TEST_F(EngineTest, ImportImageErrorCases) {
  const sysmem_util::GlobalBufferCollectionId kGlobalBufferCollectionId = 30;
  const flatland::GlobalImageId kImageId = 50;
  const uint32_t kVmoCount = 2;
  const uint32_t kVmoIdx = 1;
  const uint32_t kMaxWidth = 100;
  const uint32_t kMaxHeight = 200;
  uint32_t num_times_import_image_called = 0;

  EXPECT_CALL(*mock_display_controller_.get(),
              ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<class ::fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*mock_display_controller_.get(),
              SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Wait once for call to ImportBufferCollection, once for setting
    // the buffer collection constraints, a single valid call to
    // ImportImage() 1 invalid call to ImportImage(), and a single
    // call to ReleaseImage(). Although there are more than three
    // invalid calls to ImportImage() below, only 3 of them make it
    // all the way to the display controller, which is why we only
    // have to wait 3 times.
    for (uint32_t i = 0; i < 5; i++) {
      mock->WaitForMessage();
    }
  });

  engine_->ImportBufferCollection(kGlobalBufferCollectionId, nullptr, CreateToken());

  ImageMetadata metadata = {
      .collection_id = kGlobalBufferCollectionId,
      .identifier = kImageId,
      .vmo_idx = kVmoIdx,
      .width = 20,
      .height = 30,
  };

  // Make sure that the engine returns true if the display controller returns true.
  const uint64_t kDisplayImageId = 70;
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .WillRepeatedly(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
                                         uint64_t collection_id, uint32_t index,
                                         MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, /*display_image_id*/kDisplayImageId);
      }));
  auto result = engine_->ImportImage(metadata);
  EXPECT_TRUE(result);

  // Make sure we can release the image properly.
  EXPECT_CALL(*mock_display_controller_, ReleaseImage(kDisplayImageId)).WillOnce(Return());
  engine_->ReleaseImage(metadata.identifier);

  // Make sure that the engine returns false if the display controller returns an error
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .WillRepeatedly(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
                                         uint64_t collection_id, uint32_t index,
                                         MockDisplayController::ImportImageCallback callback) {
        callback(ZX_ERR_INVALID_ARGS, /*display_image_id*/ 0);
      }));
  result = engine_->ImportImage(metadata);
  EXPECT_FALSE(result);

  // Collection ID can't be invalid. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  auto copy_metadata = metadata;
  copy_metadata.collection_id = sysmem_util::kInvalidId;
  result = engine_->ImportImage(copy_metadata);
  EXPECT_FALSE(result);

  // Image Id can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.identifier = 0;
  result = engine_->ImportImage(copy_metadata);
  EXPECT_FALSE(result);

  // Width can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.width = 0;
  result = engine_->ImportImage(copy_metadata);
  EXPECT_FALSE(result);

  // Height can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(), ImportImage(_, _, 0, _)).Times(0);
  copy_metadata = metadata;
  copy_metadata.height = 0;
  result = engine_->ImportImage(copy_metadata);
  EXPECT_FALSE(result);

  server.join();
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

  uint64_t display_id = 1;
  glm::uvec2 resolution(1024, 768);

  // We will end up with 2 source frames, 2 destination frames, and two layers beind sent to the
  // display.
  fuchsia::hardware::display::Frame sources[2] = {
      {.x_pos = 0u, .y_pos = 0u, .width = 512, .height = 1024u},
      {.x_pos = 0u, .y_pos = 0u, .width = 128u, .height = 256u}};

  fuchsia::hardware::display::Frame destinations[2] = {
      {.x_pos = 5u, .y_pos = 7u, .width = 30, .height = 40u},
      {.x_pos = 9u, .y_pos = 13u, .width = 10u, .height = 20u}};

  // Setup the EXPECT_CALLs for gmock.
  uint64_t layer_id = 1;
  EXPECT_CALL(*mock_display_controller_.get(), CreateLayer(_))
      .WillRepeatedly(testing::Invoke([&](MockDisplayController::CreateLayerCallback callback) {
        callback(ZX_OK, layer_id++);
      }));

  std::vector<uint64_t> layers = {1u, 2u};
  EXPECT_CALL(*mock_display_controller_.get(), SetDisplayLayers(display_id, layers)).Times(1);

  // Unfortunately, |fuchsia::hardware::display::Frame| doesn't have an equality operator, so
  // we can't just pass in the values we're expecting into the function as parameters. We can
  // still use fidl::Equals inside the function body, however.
  EXPECT_CALL(*mock_display_controller_.get(), SetLayerPrimaryPosition(layers[0], _, _, _))
      .WillOnce(
          testing::Invoke([&](uint64_t layer_id, fuchsia::hardware::display::Transform transform,
                              fuchsia::hardware::display::Frame src_frame,
                              fuchsia::hardware::display::Frame dest_frame) {
            EXPECT_TRUE(fidl::Equals(src_frame, sources[0]));
            EXPECT_TRUE(fidl::Equals(dest_frame, destinations[0]));
          }));

  EXPECT_CALL(*mock_display_controller_.get(), SetLayerPrimaryPosition(layers[1], _, _, _))
      .WillOnce(
          testing::Invoke([&](uint64_t layer_id, fuchsia::hardware::display::Transform transform,
                              fuchsia::hardware::display::Frame src_frame,
                              fuchsia::hardware::display::Frame dest_frame) {
            EXPECT_TRUE(fidl::Equals(src_frame, sources[1]));
            EXPECT_TRUE(fidl::Equals(dest_frame, destinations[1]));
          }));

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Since we have 2 rectangles with images, we have to wait for 2 calls to initialize layers,
    // 1 call to set the layers on the display, and 2 calls to set the layer primary positions.
    // This all happens when we call engine_->RenderFrame() below.
    for (uint32_t i = 0; i < 5; i++) {
      mock->WaitForMessage();
    }
  });

  engine_->AddDisplay(display_id, parent_root_handle, resolution);
  engine_->RenderFrame();

  server.join();
}

}  // namespace test
}  // namespace flatland
