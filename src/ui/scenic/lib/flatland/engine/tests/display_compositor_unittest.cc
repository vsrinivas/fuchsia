// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/engine/tests/common.h"
#include "src/ui/scenic/lib/flatland/engine/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/renderer/mock_renderer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using ::testing::_;
using ::testing::Return;

using allocation::ImageMetadata;
using flatland::LinkSystem;
using flatland::MockDisplayController;
using flatland::Renderer;
using flatland::TransformGraph;
using flatland::TransformHandle;
using flatland::UberStruct;
using flatland::UberStructSystem;
using fuchsia::ui::composition::ChildViewStatus;
using fuchsia::ui::composition::ChildViewWatcher;
using fuchsia::ui::composition::LayoutInfo;
using fuchsia::ui::composition::ParentViewportWatcher;
using fuchsia::ui::views::ViewCreationToken;
using fuchsia::ui::views::ViewportCreationToken;

namespace flatland {
namespace test {

class DisplayCompositorTest : public DisplayCompositorTestBase {
 public:
  void SetUp() override {
    DisplayCompositorTestBase::SetUp();

    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr();

    renderer_ = std::make_shared<flatland::MockRenderer>();

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

    display_compositor_ = std::make_unique<flatland::DisplayCompositor>(
        dispatcher(), std::move(shared_display_controller), renderer_,
        utils::CreateSysmemAllocatorSyncPtr("display_compositor_unittest"),
        BufferCollectionImportMode::AttemptDisplayConstraints);
  }

  void TearDown() override {
    renderer_.reset();
    display_compositor_.reset();
    mock_display_controller_.reset();

    DisplayCompositorTestBase::TearDown();
  }

  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> CreateToken() {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token;
    zx_status_t status = sysmem_allocator_->AllocateSharedCollection(token.NewRequest());
    FX_DCHECK(status == ZX_OK);
    status = token->Sync();
    FX_DCHECK(status == ZX_OK);
    return token;
  }

  void SetDisplaySupported(allocation::GlobalBufferCollectionId id, bool is_supported) {
    display_compositor_->buffer_collection_supports_display_[id] = is_supported;
  }

  void SetBufferCollectionImportMode(BufferCollectionImportMode import_mode) {
    display_compositor_->import_mode_ = import_mode;
  }

 protected:
  const zx_pixel_format_t kPixelFormat = ZX_PIXEL_FORMAT_RGB_x888;
  std::unique_ptr<flatland::MockDisplayController> mock_display_controller_;
  std::shared_ptr<flatland::MockRenderer> renderer_;
  std::unique_ptr<flatland::DisplayCompositor> display_compositor_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

class ParameterizedDisplayCompositorTest
    : public DisplayCompositorTest,
      public ::testing::WithParamInterface<BufferCollectionImportMode> {};

TEST_P(ParameterizedDisplayCompositorTest, ImportAndReleaseBufferCollectionTest) {
  SetBufferCollectionImportMode(GetParam());
  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // Wait once for call to ImportBufferCollection, once for setting the
    // constraints, and once for call to ReleaseBufferCollection. Finally
    // one call for the deleter.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 4; i++) {
      mock->WaitForMessage();
    }
  });

  const allocation::GlobalBufferCollectionId kGlobalBufferCollectionId = 15;

  EXPECT_CALL(*mock_display_controller_.get(),
              ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*mock_display_controller_.get(),
              SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  // Save token to avoid early token failure.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken());

  EXPECT_CALL(*mock_display_controller_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId);

  EXPECT_CALL(*mock_display_controller_.get(), CheckConfig(_, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  display_compositor_.reset();
  server.join();
}

INSTANTIATE_TEST_SUITE_P(BufferCollectionImportModes, ParameterizedDisplayCompositorTest,
                         ::testing::Values(BufferCollectionImportMode::EnforceDisplayConstraints,
                                           BufferCollectionImportMode::AttemptDisplayConstraints));

TEST_F(DisplayCompositorTest, ImageIsValidAfterReleaseBufferCollection) {
  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // Wait once for call to ImportBufferCollection, once for setting the constraints, once for
    // hardware, and once for call to ReleaseBufferCollection. Finally one call for the deleter.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 5; i++) {
      mock->WaitForMessage();
    }
  });

  const auto kGlobalBufferCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Import buffer collection.
  EXPECT_CALL(*mock, ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*mock, SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  // Save token to avoid early token failure.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken());
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  // Import image.
  ImageMetadata image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
  };
  const uint64_t kDisplayImageId = 2;
  EXPECT_CALL(*mock, ImportImage(_, kGlobalBufferCollectionId, 0, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, kDisplayImageId);
      }));
  EXPECT_CALL(*renderer_.get(), ImportBufferImage(image_metadata)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(image_metadata);

  // Release buffer collection. Make sure that does not release Image.
  EXPECT_CALL(*mock, ReleaseImage(kDisplayImageId)).Times(0);
  EXPECT_CALL(*mock, ReleaseBufferCollection(kGlobalBufferCollectionId)).WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId);

  EXPECT_CALL(*mock, CheckConfig(_, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  display_compositor_.reset();
  server.join();
}

TEST_F(DisplayCompositorTest, ImportImageErrorCases) {
  const allocation::GlobalBufferCollectionId kGlobalBufferCollectionId = 30;
  const allocation::GlobalImageId kImageId = 50;
  const uint32_t kVmoCount = 2;
  const uint32_t kVmoIdx = 1;
  const uint32_t kMaxWidth = 100;
  const uint32_t kMaxHeight = 200;
  uint32_t num_times_import_image_called = 0;

  EXPECT_CALL(*mock_display_controller_.get(),
              ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*mock_display_controller_.get(),
              SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  // Save token to avoid early token failure.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        token_ref = std::move(token);
        return true;
      });

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Wait once for call to ImportBufferCollection, once for setting
    // the buffer collection constraints, a single valid call to
    // ImportBufferImage() 1 invalid call to ImportBufferImage(), and a single
    // call to ReleaseBufferImage(). Although there are more than three
    // invalid calls to ImportBufferImage() below, only 3 of them make it
    // all the way to the display controller, which is why we only
    // have to wait 3 times. Finally add one call for the deleter.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 6; i++) {
      mock->WaitForMessage();
    }
  });

  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken());
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  ImageMetadata metadata = {
      .collection_id = kGlobalBufferCollectionId,
      .identifier = kImageId,
      .vmo_index = kVmoIdx,
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

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(metadata)).WillOnce(Return(true));

  auto result = display_compositor_->ImportBufferImage(metadata);
  EXPECT_TRUE(result);

  // Make sure we can release the image properly.
  EXPECT_CALL(*mock_display_controller_, ReleaseImage(kDisplayImageId)).WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferImage(metadata.identifier)).WillOnce(Return());

  display_compositor_->ReleaseBufferImage(metadata.identifier);

  // Make sure that the engine returns false if the display controller returns an error
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
                                   uint64_t collection_id, uint32_t index,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_ERR_INVALID_ARGS, /*display_image_id*/ 0);
      }));

  // This should still return false for the engine even if the renderer returns true.
  EXPECT_CALL(*renderer_.get(), ImportBufferImage(metadata)).WillOnce(Return(true));

  result = display_compositor_->ImportBufferImage(metadata);
  EXPECT_FALSE(result);

  // Collection ID can't be invalid. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  auto copy_metadata = metadata;
  copy_metadata.collection_id = allocation::kInvalidId;
  result = display_compositor_->ImportBufferImage(copy_metadata);
  EXPECT_FALSE(result);

  // Image Id can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.identifier = 0;
  result = display_compositor_->ImportBufferImage(copy_metadata);
  EXPECT_FALSE(result);

  // Width can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage(_, kGlobalBufferCollectionId, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.width = 0;
  result = display_compositor_->ImportBufferImage(copy_metadata);
  EXPECT_FALSE(result);

  // Height can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(), ImportImage(_, _, 0, _)).Times(0);
  copy_metadata = metadata;
  copy_metadata.height = 0;
  result = display_compositor_->ImportBufferImage(copy_metadata);
  EXPECT_FALSE(result);

  EXPECT_CALL(*mock_display_controller_, CheckConfig(_, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  display_compositor_.reset();

  server.join();
}

// When compositing directly to a hardware display layer, the display controller
// takes in source and destination Frame object types, which mirrors flatland usage.
// The source frames are nonnormalized UV coordinates and the destination frames are
// screenspace coordinates given in pixels. So this test makes sure that the rectangle
// and frame data that is generated by flatland sends along to the display controller
// the proper source and destination frame data. Each source and destination frame pair
// should be added to its own layer on the display.
TEST_F(DisplayCompositorTest, HardwareFrameCorrectnessTest) {
  const uint64_t kGlobalBufferCollectionId = 1;

  // Create a parent and child session.
  auto parent_session = CreateSession();
  auto child_session = CreateSession();

  // Create a link between the two.
  auto child_link = child_session.CreateView(parent_session);

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
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
  };
  parent_struct->images[parent_image_handle] = parent_image_metadata;

  parent_struct->local_matrices[parent_image_handle] =
      glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(9, 13)), glm::vec2(10, 20));
  parent_struct->local_image_sample_regions[parent_image_handle] = {0, 0, 128, 256};

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  // Get an UberStruct for the child session. Note that the argument will be ignored anyway.
  auto child_struct =
      child_session.CreateUberStructWithCurrentTopology(child_session.GetLinkOrigin());

  // Add an image.
  ImageMetadata child_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 1,
      .width = 512,
      .height = 1024,
  };
  child_struct->images[child_image_handle] = child_image_metadata;
  child_struct->local_matrices[child_image_handle] =
      glm::scale(glm::translate(glm::mat3(1), glm::vec2(5, 7)), glm::vec2(30, 40));
  child_struct->local_image_sample_regions[child_image_handle] = {0, 0, 512, 1024};

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
    // - 1 call to discard the config.
    // - 2 calls to set each layer image
    // - 2 calls to set the layer primary config
    // - 2 calls to set the layer primary alpha.
    // - 2 calls to set the layer primary positions
    // - 1 call to check the config
    // - 1 call to apply the config
    // - 1 call to DiscardConfig
    // -2 calls to destroy layer.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 21; i++) {
      mock->WaitForMessage();
    }
  });

  EXPECT_CALL(*mock, ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken>,
             MockDisplayController::ImportBufferCollectionCallback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*mock, SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  // Save token to avoid early token failure.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken());
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  const uint64_t kParentDisplayImageId = 2;
  EXPECT_CALL(*mock, ImportImage(_, kGlobalBufferCollectionId, 0, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, kParentDisplayImageId);
      }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(parent_image_metadata)).WillOnce(Return(true));

  display_compositor_->ImportBufferImage(parent_image_metadata);

  const uint64_t kChildDisplayImageId = 3;
  EXPECT_CALL(*mock, ImportImage(_, kGlobalBufferCollectionId, 1, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig, uint64_t, uint32_t,
                                   MockDisplayController::ImportImageCallback callback) {
        callback(ZX_OK, kChildDisplayImageId);
      }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(child_image_metadata)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(child_image_metadata);

  // We start the frame by clearing the config.
  EXPECT_CALL(*mock, CheckConfig(true, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

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

  DisplayInfo display_info = {resolution, {kPixelFormat}};
  display_compositor_->AddDisplay(display_id, display_info, /*num_vmos*/ 0,
                                  /*out_buffer_collection*/ nullptr);

  display_compositor_->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest({{display_id, {display_info, parent_root_handle}}}), {},
      [](const scheduling::FrameRenderer::Timestamps&) {});

  for (uint32_t i = 0; i < 2; i++) {
    EXPECT_CALL(*mock, DestroyLayer(layers[i]));
  }

  EXPECT_CALL(*mock_display_controller_, CheckConfig(_, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  display_compositor_.reset();

  server.join();
}

// Tests that RenderOnly mode does not attempt to ImportBufferCollection() to display.
TEST_F(DisplayCompositorTest, RendererOnly_ImportAndReleaseBufferCollectionTest) {
  SetBufferCollectionImportMode(BufferCollectionImportMode::RendererOnly);

  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // Wait once for call to ReleaseBufferCollection and once for the deleter.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 2; i++) {
      mock->WaitForMessage();
    }
  });

  const allocation::GlobalBufferCollectionId kGlobalBufferCollectionId = 15;

  EXPECT_CALL(*mock_display_controller_.get(),
              ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .Times(0);
  // Save token to avoid early token failure.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken());

  EXPECT_CALL(*mock_display_controller_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId);

  EXPECT_CALL(*mock_display_controller_.get(), CheckConfig(_, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));
  display_compositor_.reset();
  server.join();
}

}  // namespace test
}  // namespace flatland
