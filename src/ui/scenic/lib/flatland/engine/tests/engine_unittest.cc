// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/engine/tests/common.h"
#include "src/ui/scenic/lib/flatland/engine/tests/mock_display_controller.h"

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

namespace flatland {
namespace test {

class EngineTest : public EngineTestBase {
 public:
  void SetUp() override {
    EngineTestBase::SetUp();

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

    auto shared_display_controller =
        std::make_shared<fuchsia::hardware::display::ControllerSyncPtr>();
    shared_display_controller->Bind(std::move(controller_channel_client));

    engine_ = std::make_unique<flatland::Engine>(std::move(shared_display_controller), renderer_,
                                                 link_system(), uber_struct_system());
  }

  void TearDown() override {
    renderer_.reset();
    engine_.reset();
    mock_display_controller_.reset();

    EngineTestBase::TearDown();
  }

 protected:
  std::unique_ptr<flatland::MockDisplayController> mock_display_controller_;
  std::shared_ptr<flatland::NullRenderer> renderer_;
  std::unique_ptr<flatland::Engine> engine_;
};

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
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
                                   uint64_t collection_id, uint32_t index,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, /*display_image_id*/ kDisplayImageId);
      }));
  auto result = engine_->ImportImage(metadata);
  EXPECT_TRUE(result);

  // Make sure we can release the image properly.
  EXPECT_CALL(*mock_display_controller_, ReleaseImage(kDisplayImageId)).WillOnce(Return());
  engine_->ReleaseImage(metadata.identifier);

  // Make sure that the engine returns false if the display controller returns an error
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
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
  const uint64_t kGlobalBufferCollectionId = 1;

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
  ImageMetadata parent_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = 1,
      .vmo_idx = 0,
      .width = 128,
      .height = 256,
  };
  parent_struct->images[parent_image_handle] = parent_image_metadata;

  parent_struct->local_matrices[parent_image_handle] =
      glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(9, 13)), glm::vec2(10, 20));

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  // Get an UberStruct for the child session. Note that the argument will be ignored anyway.
  auto child_struct =
      child_session.CreateUberStructWithCurrentTopology(child_session.GetLinkOrigin());

  // Add an image.
  ImageMetadata child_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = 2,
      .vmo_idx = 1,
      .width = 512,
      .height = 1024,
  };
  child_struct->images[child_image_handle] = child_image_metadata;
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

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Since we have 2 rectangles with images with 1 buffer collection, we have to wait
    // for...:
    // - 2 calls for importing and setting constraints on the collection
    // - 2 calls to import the images
    // - 2 calls to initialize layers
    // - 1 call to set the layers on the display
    // - 2 calls to set each layer image
    // - 2 calls to set the layer primary config
    // - 2 calls to set the layer primary alpha.
    // - 2 calls to set the layer primary positions
    // - 1 call to check the config
    // - 1 call to apply the config
    for (uint32_t i = 0; i < 17; i++) {
      mock->WaitForMessage();
    }
  });

  EXPECT_CALL(*mock, ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<class ::fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*mock, SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  engine_->ImportBufferCollection(kGlobalBufferCollectionId, nullptr, CreateToken());

  const uint64_t kParentDisplayImageId = 2;
  EXPECT_CALL(*mock, ImportImage(_, kGlobalBufferCollectionId, 0, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, kParentDisplayImageId);
      }));

  engine_->ImportImage(parent_image_metadata);

  const uint64_t kChildDisplayImageId = 3;
  EXPECT_CALL(*mock, ImportImage(_, kGlobalBufferCollectionId, 1, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, kChildDisplayImageId);
      }));
  engine_->ImportImage(child_image_metadata);

  // Setup the EXPECT_CALLs for gmock.
  uint64_t layer_id = 1;
  EXPECT_CALL(*mock, CreateLayer(_))
      .WillRepeatedly(testing::Invoke([&](MockDisplayController::CreateLayerCallback callback) {
        callback(ZX_OK, layer_id++);
      }));

  std::vector<uint64_t> layers = {1u, 2u};
  EXPECT_CALL(*mock, SetDisplayLayers(display_id, layers)).Times(1);

  // Make sure each layer has all of its components set properly.
  uint64_t collection_ids[] = {kChildDisplayImageId, kParentDisplayImageId};
  for (uint32_t i = 0; i < 2; i++) {
    EXPECT_CALL(*mock, SetLayerPrimaryConfig(layers[i], _)).Times(1);
    EXPECT_CALL(*mock, SetLayerPrimaryPosition(layers[i], _, _, _))
        .WillOnce(
            testing::Invoke([sources, destinations, index = i](
                                uint64_t layer_id, fuchsia::hardware::display::Transform transform,
                                fuchsia::hardware::display::Frame src_frame,
                                fuchsia::hardware::display::Frame dest_frame) {
              EXPECT_TRUE(fidl::Equals(src_frame, sources[index]));
              EXPECT_TRUE(fidl::Equals(dest_frame, destinations[index]));
            }));
    EXPECT_CALL(*mock, SetLayerPrimaryAlpha(layers[i], _, _)).Times(1);
    EXPECT_CALL(*mock, SetLayerImage(layers[i], collection_ids[i], _, _)).Times(1);
  }

  EXPECT_CALL(*mock, CheckConfig(false, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  EXPECT_CALL(*mock, ApplyConfig()).WillOnce(Return());

  engine_->AddDisplay(display_id, parent_root_handle, resolution);
  engine_->RenderFrame();

  server.join();
}

}  // namespace test
}  // namespace flatland
