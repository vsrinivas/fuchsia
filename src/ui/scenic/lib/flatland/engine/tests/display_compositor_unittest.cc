// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "src/lib/fsl/handles/object_info.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/display/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/flatland/engine/tests/common.h"
#include "src/ui/scenic/lib/flatland/engine/tests/mock_display_controller.h"
#include "src/ui/scenic/lib/flatland/renderer/mock_renderer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using ::testing::_;
using ::testing::Return;

using allocation::BufferCollectionUsage;
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
using fhd_Transform = fuchsia::hardware::display::Transform;

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

    display_compositor_ = std::make_shared<flatland::DisplayCompositor>(
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
    display_compositor_->buffer_collection_pixel_format_[id] = fuchsia::sysmem::PixelFormat{
        .type = fuchsia::sysmem::PixelFormatType::BGRA32,
    };
  }

  void SetBufferCollectionImportMode(BufferCollectionImportMode import_mode) {
    display_compositor_->import_mode_ = import_mode;
  }

  void SendOnVsyncEvent(fuchsia::hardware::display::ConfigStamp stamp) {
    display_compositor_->OnVsync(zx::time(), stamp);
  }

  std::deque<DisplayCompositor::ApplyConfigInfo> GetPendingApplyConfigs() {
    return display_compositor_->pending_apply_configs_;
  }

 protected:
  const zx_pixel_format_t kPixelFormat = ZX_PIXEL_FORMAT_RGB_x888;
  std::unique_ptr<flatland::MockDisplayController> mock_display_controller_;
  std::shared_ptr<flatland::MockRenderer> renderer_;
  std::shared_ptr<flatland::DisplayCompositor> display_compositor_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  void HardwareFrameCorrectnessWithRotationTester(glm::mat3 transform_matrix,
                                                  fuchsia::hardware::display::Frame expected_dst,
                                                  fhd_Transform expected_transform);
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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);

  EXPECT_CALL(*mock_display_controller_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId, _))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId,
                                               BufferCollectionUsage::kClientImage);

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

TEST_F(DisplayCompositorTest, ClientDropSysmemToken) {
  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // Wait once for call to deleter.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 1; i++) {
      mock->WaitForMessage();
    }
  });

  const auto kGlobalBufferCollectionId = allocation::GenerateUniqueBufferCollectionId();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  // Let client drop token.
  {
    auto token = CreateToken();
    auto sync_token = token.BindSync();
    sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, dup_token.NewRequest());
    sync_token->Sync();
  }

  // Save token to avoid early token failure in Renderer import.
  fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token_ref;
  ON_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillByDefault(
          [&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                       fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                       BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
            token_ref = std::move(token);
            return true;
          });
  EXPECT_FALSE(display_compositor_->ImportBufferCollection(
      kGlobalBufferCollectionId, sysmem_allocator_.get(), std::move(dup_token),
      BufferCollectionUsage::kClientImage, std::nullopt));

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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  // Import image.
  ImageMetadata image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };
  EXPECT_CALL(*mock, ImportImage2(_, kGlobalBufferCollectionId, _, 0, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig, uint64_t, uint64_t, uint32_t,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));
  EXPECT_CALL(*renderer_.get(), ImportBufferImage(image_metadata, _)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);

  // Release buffer collection. Make sure that does not release Image.
  EXPECT_CALL(*mock, ReleaseImage(image_metadata.identifier)).Times(0);
  EXPECT_CALL(*mock, ReleaseBufferCollection(kGlobalBufferCollectionId)).WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId, _))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId,
                                               BufferCollectionUsage::kClientImage);

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
  const allocation::GlobalBufferCollectionId kGlobalBufferCollectionId =
      allocation::GenerateUniqueBufferCollectionId();
  const allocation::GlobalImageId kImageId = allocation::GenerateUniqueImageId();
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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
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
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  ImageMetadata metadata = {
      .collection_id = kGlobalBufferCollectionId,
      .identifier = kImageId,
      .vmo_index = kVmoIdx,
      .width = 20,
      .height = 30,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };

  // Make sure that the engine returns true if the display controller returns true.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage2(_, kGlobalBufferCollectionId, kImageId, kVmoIdx, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig image_config, uint64_t collection_id,
             uint64_t image_id, uint32_t index,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(metadata, _)).WillOnce(Return(true));

  auto result =
      display_compositor_->ImportBufferImage(metadata, BufferCollectionUsage::kClientImage);
  EXPECT_TRUE(result);

  // Make sure we can release the image properly.
  EXPECT_CALL(*mock_display_controller_, ReleaseImage(kImageId)).WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferImage(metadata.identifier)).WillOnce(Return());

  display_compositor_->ReleaseBufferImage(metadata.identifier);

  // Make sure that the engine returns false if the display controller returns an error
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage2(_, kGlobalBufferCollectionId, _, kVmoIdx, _))
      .WillOnce(testing::Invoke([](fuchsia::hardware::display::ImageConfig image_config,
                                   uint64_t collection_id, uint64_t image_id, uint32_t index,
                                   MockDisplayController::ImportImage2Callback callback) {
        callback(ZX_ERR_INVALID_ARGS);
      }));

  // This should still return false for the engine even if the renderer returns true.
  EXPECT_CALL(*renderer_.get(), ImportBufferImage(metadata, _)).WillOnce(Return(true));

  result = display_compositor_->ImportBufferImage(metadata, BufferCollectionUsage::kClientImage);
  EXPECT_FALSE(result);

  // Collection ID can't be invalid. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage2(_, kGlobalBufferCollectionId, _, kVmoIdx, _))
      .Times(0);
  auto copy_metadata = metadata;
  copy_metadata.collection_id = allocation::kInvalidId;
  result =
      display_compositor_->ImportBufferImage(copy_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_FALSE(result);

  // Image Id can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage2(_, kGlobalBufferCollectionId, _, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.identifier = allocation::kInvalidImageId;
  result =
      display_compositor_->ImportBufferImage(copy_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_FALSE(result);

  // Width can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(),
              ImportImage2(_, kGlobalBufferCollectionId, _, kVmoIdx, _))
      .Times(0);
  copy_metadata = metadata;
  copy_metadata.width = 0;
  result =
      display_compositor_->ImportBufferImage(copy_metadata, BufferCollectionUsage::kClientImage);
  EXPECT_FALSE(result);

  // Height can't be 0. This shouldn't reach the display controller.
  EXPECT_CALL(*mock_display_controller_.get(), ImportImage2(_, _, _, 0, _)).Times(0);
  copy_metadata = metadata;
  copy_metadata.height = 0;
  result =
      display_compositor_->ImportBufferImage(copy_metadata, BufferCollectionUsage::kClientImage);
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

// This test checks that DisplayCompositor properly processes ConfigStamp from Vsync.
TEST_F(DisplayCompositorTest, VsyncConfigStampAreProcessed) {
  auto session = CreateSession();
  const TransformHandle root_handle = session.graph().CreateTransform();
  uint64_t display_id = 1;
  glm::uvec2 resolution(1024, 768);
  DisplayInfo display_info = {resolution, {kPixelFormat}};

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // We have to wait for 2 times:
    // - 2 calls to DiscardConfig
    // - 2 calls to CheckConfig
    // - 2 calls to ApplyConfig
    // - 2 calls to GetLatestAppliedConfigStamp
    // - 1 call to DiscardConfig
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 9; i++) {
      mock->WaitForMessage();
    }
  });

  EXPECT_CALL(*mock, CheckConfig(_, _))
      .WillRepeatedly(
          testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
            fuchsia::hardware::display::ConfigResult result =
                fuchsia::hardware::display::ConfigResult::OK;
            std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
            callback(result, ops);
          }));
  EXPECT_CALL(*mock, ApplyConfig()).WillRepeatedly(Return());

  const uint64_t kConfigStamp1 = 234;
  EXPECT_CALL(*mock, GetLatestAppliedConfigStamp(_))
      .WillOnce(
          testing::Invoke([&](MockDisplayController::GetLatestAppliedConfigStampCallback callback) {
            fuchsia::hardware::display::ConfigStamp stamp = {kConfigStamp1};
            callback(stamp);
          }));
  display_compositor_->RenderFrame(1, zx::time(1), {}, {},
                                   [](const scheduling::FrameRenderer::Timestamps&) {});

  const uint64_t kConfigStamp2 = 123;
  EXPECT_CALL(*mock, GetLatestAppliedConfigStamp(_))
      .WillOnce(
          testing::Invoke([&](MockDisplayController::GetLatestAppliedConfigStampCallback callback) {
            fuchsia::hardware::display::ConfigStamp stamp = {kConfigStamp2};
            callback(stamp);
          }));
  display_compositor_->RenderFrame(2, zx::time(2), {}, {},
                                   [](const scheduling::FrameRenderer::Timestamps&) {});

  EXPECT_EQ(2u, GetPendingApplyConfigs().size());

  // Sending another vsync should be skipped.
  const uint64_t kConfigStamp3 = 345;
  SendOnVsyncEvent({kConfigStamp3});
  EXPECT_EQ(2u, GetPendingApplyConfigs().size());

  // Sending later vsync should signal and remove the earlier one too.
  SendOnVsyncEvent({kConfigStamp2});
  EXPECT_EQ(0u, GetPendingApplyConfigs().size());

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
  const uint64_t kGlobalBufferCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Create a parent and child session.
  auto parent_session = CreateSession();
  auto child_session = CreateSession();

  // Create a link between the two.
  auto link_to_child = child_session.CreateView(parent_session);

  // Create the root handle for the parent and a handle that will have an image attached.
  const TransformHandle parent_root_handle = parent_session.graph().CreateTransform();
  const TransformHandle parent_image_handle = parent_session.graph().CreateTransform();

  // Add the two children to the parent root: link, then image.
  parent_session.graph().AddChild(parent_root_handle, link_to_child.GetInternalLinkHandle());
  parent_session.graph().AddChild(parent_root_handle, parent_image_handle);

  // Create an image handle for the child.
  const TransformHandle child_image_handle = child_session.graph().CreateTransform();

  // Attach that image handle to the child link transform handle.
  child_session.graph().AddChild(child_session.GetLinkChildTransformHandle(), child_image_handle);

  // Get an UberStruct for the parent session.
  auto parent_struct = parent_session.CreateUberStructWithCurrentTopology(parent_root_handle);

  // Add an image.
  ImageMetadata parent_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };
  parent_struct->images[parent_image_handle] = parent_image_metadata;

  parent_struct->local_matrices[parent_image_handle] =
      glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(9, 13)), glm::vec2(10, 20));
  parent_struct->local_image_sample_regions[parent_image_handle] = {0, 0, 128, 256};

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  // Get an UberStruct for the child session. Note that the argument will be ignored anyway.
  auto child_struct = child_session.CreateUberStructWithCurrentTopology(
      child_session.GetLinkChildTransformHandle());

  // Add an image.
  ImageMetadata child_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 1,
      .width = 512,
      .height = 1024,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
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
    // - 2 calls to create layers.
    // - 1 call to discard the config.
    // - 1 call to set the layers on the display
    // - 2 calls to import events for images.
    // - 2 calls to set each layer image
    // - 2 calls to set the layer primary config
    // - 2 calls to set the layer primary positions
    // - 2 calls to set the layer primary alpha.
    // - 1 call to SetDisplayColorConversion
    // - 1 call to check the config
    // - 1 call to apply the config
    // - 1 call to GetLatestAppliedConfigStamp
    // - 1 call to DiscardConfig
    // - 2 calls to destroy layer.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 25; i++) {
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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  EXPECT_CALL(*mock,
              ImportImage2(_, kGlobalBufferCollectionId, parent_image_metadata.identifier, 0, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig, uint64_t, uint64_t, uint32_t,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(parent_image_metadata, _)).WillOnce(Return(true));

  display_compositor_->ImportBufferImage(parent_image_metadata,
                                         BufferCollectionUsage::kClientImage);

  EXPECT_CALL(*mock,
              ImportImage2(_, kGlobalBufferCollectionId, child_image_metadata.identifier, 1, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig, uint64_t, uint64_t, uint32_t,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(child_image_metadata, _)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(child_image_metadata, BufferCollectionUsage::kClientImage);

  display_compositor_->SetColorConversionValues({1, 0, 0, 0, 1, 0, 0, 0, 1}, {0.1f, 0.2f, 0.3f},
                                                {-0.3f, -0.2f, -0.1f});

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
  uint64_t collection_ids[] = {child_image_metadata.identifier, parent_image_metadata.identifier};
  for (uint32_t i = 0; i < 2; i++) {
    EXPECT_CALL(*mock, SetLayerPrimaryConfig(layers[i], _)).Times(1);
    EXPECT_CALL(*mock, SetLayerPrimaryPosition(layers[i], fhd_Transform::IDENTITY, _, _))
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
  EXPECT_CALL(*mock, ImportEvent(_, _)).Times(2);

  EXPECT_CALL(*mock, SetDisplayColorConversion(_, _, _, _)).Times(1);

  EXPECT_CALL(*mock, CheckConfig(false, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  EXPECT_CALL(*renderer_.get(), ChoosePreferredPixelFormat(_));

  DisplayInfo display_info = {resolution, {kPixelFormat}};
  scenic_impl::display::Display display(display_id, resolution.x, resolution.y);
  display_compositor_->AddDisplay(&display, display_info, /*num_vmos*/ 0,
                                  /*out_buffer_collection*/ nullptr);

  EXPECT_CALL(*mock, ApplyConfig()).WillOnce(Return());
  EXPECT_CALL(*mock, GetLatestAppliedConfigStamp(_))
      .WillOnce(
          testing::Invoke([&](MockDisplayController::GetLatestAppliedConfigStampCallback callback) {
            fuchsia::hardware::display::ConfigStamp stamp = {1};
            callback(stamp);
          }));

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

void DisplayCompositorTest::HardwareFrameCorrectnessWithRotationTester(
    glm::mat3 transform_matrix, fuchsia::hardware::display::Frame expected_dst,
    fhd_Transform expected_transform) {
  const uint64_t kGlobalBufferCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Create a parent session.
  auto parent_session = CreateSession();

  // Create the root handle for the parent and a handle that will have an image attached.
  const TransformHandle parent_root_handle = parent_session.graph().CreateTransform();
  const TransformHandle parent_image_handle = parent_session.graph().CreateTransform();

  // Add the image to the parent.
  parent_session.graph().AddChild(parent_root_handle, parent_image_handle);

  // Get an UberStruct for the parent session.
  auto parent_struct = parent_session.CreateUberStructWithCurrentTopology(parent_root_handle);

  // Add an image.
  ImageMetadata parent_image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };
  parent_struct->images[parent_image_handle] = parent_image_metadata;

  parent_struct->local_matrices[parent_image_handle] = std::move(transform_matrix);
  parent_struct->local_image_sample_regions[parent_image_handle] = {0, 0, 128, 256};

  // Submit the UberStruct.
  parent_session.PushUberStruct(std::move(parent_struct));

  uint64_t display_id = 1;
  glm::uvec2 resolution(1024, 768);

  // We will end up with 1 source frame, 1 destination frame, and one layer being sent to the
  // display.
  fuchsia::hardware::display::Frame source = {
      .x_pos = 0u, .y_pos = 0u, .width = 128u, .height = 256u};

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Since we have 1 rectangles with images with 1 buffer collection, we have to wait
    // for...:
    // - 2 calls for importing and setting constraints on the collection
    // - 1 calls to import the images
    // - 2 calls to create layers (a new display creates two layers upfront).
    // - 1 call to discard the config.
    // - 1 call to set the layers on the display
    // - 1 calls to import events for images.
    // - 1 calls to set each layer image
    // - 1 calls to set the layer primary config
    // - 1 calls to set the layer primary positions
    // - 1 calls to set the layer primary alpha.
    // - 1 call to SetDisplayColorConversion
    // - 1 call to check the config
    // - 1 call to apply the config
    // - 1 call to GetLatestAppliedConfigStamp
    // - 1 call to DiscardConfig
    // - 1 calls to destroy layer.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 18; i++) {
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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  EXPECT_CALL(*mock,
              ImportImage2(_, kGlobalBufferCollectionId, parent_image_metadata.identifier, 0, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig, uint64_t, uint64_t, uint32_t,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(parent_image_metadata, _)).WillOnce(Return(true));

  display_compositor_->ImportBufferImage(parent_image_metadata,
                                         BufferCollectionUsage::kClientImage);

  display_compositor_->SetColorConversionValues({1, 0, 0, 0, 1, 0, 0, 0, 1}, {0.1f, 0.2f, 0.3f},
                                                {-0.3f, -0.2f, -0.1f});

  // We start the frame by clearing the config.
  EXPECT_CALL(*mock, CheckConfig(true, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  // Setup the EXPECT_CALLs for gmock.
  // Note that a couple of layers are created upfront for the display.
  uint64_t layer_id = 1;
  EXPECT_CALL(*mock, CreateLayer(_))
      .WillRepeatedly(testing::Invoke([&](MockDisplayController::CreateLayerCallback callback) {
        callback(ZX_OK, layer_id++);
      }));

  // However, we only set one display layer for the image.
  std::vector<uint64_t> layers = {1u};
  EXPECT_CALL(*mock, SetDisplayLayers(display_id, layers)).Times(1);

  uint64_t collection_id = parent_image_metadata.identifier;
  EXPECT_CALL(*mock, SetLayerPrimaryConfig(layers[0], _)).Times(1);
  EXPECT_CALL(*mock, SetLayerPrimaryPosition(layers[0], expected_transform, _, _))
      .WillOnce(testing::Invoke(
          [source, expected_dst](uint64_t layer_id, fuchsia::hardware::display::Transform transform,
                                 fuchsia::hardware::display::Frame src_frame,
                                 fuchsia::hardware::display::Frame dest_frame) {
            EXPECT_TRUE(fidl::Equals(src_frame, source));
            EXPECT_TRUE(fidl::Equals(dest_frame, expected_dst));
          }));
  EXPECT_CALL(*mock, SetLayerPrimaryAlpha(layers[0], _, _)).Times(1);
  EXPECT_CALL(*mock, SetLayerImage(layers[0], collection_id, _, _)).Times(1);
  EXPECT_CALL(*mock, ImportEvent(_, _)).Times(1);

  EXPECT_CALL(*mock, SetDisplayColorConversion(_, _, _, _)).Times(1);

  EXPECT_CALL(*mock, CheckConfig(false, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));

  EXPECT_CALL(*renderer_.get(), ChoosePreferredPixelFormat(_));

  DisplayInfo display_info = {resolution, {kPixelFormat}};
  scenic_impl::display::Display display(display_id, resolution.x, resolution.y);
  display_compositor_->AddDisplay(&display, display_info, /*num_vmos*/ 0,
                                  /*out_buffer_collection*/ nullptr);

  EXPECT_CALL(*mock, ApplyConfig()).WillOnce(Return());
  EXPECT_CALL(*mock, GetLatestAppliedConfigStamp(_))
      .WillOnce(
          testing::Invoke([&](MockDisplayController::GetLatestAppliedConfigStampCallback callback) {
            fuchsia::hardware::display::ConfigStamp stamp = {1};
            callback(stamp);
          }));

  display_compositor_->RenderFrame(
      1, zx::time(1),
      GenerateDisplayListForTest({{display_id, {display_info, parent_root_handle}}}), {},
      [](const scheduling::FrameRenderer::Timestamps&) {});

  EXPECT_CALL(*mock, DestroyLayer(layers[0]));

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

TEST_F(DisplayCompositorTest, HardwareFrameCorrectnessWith90DegreeRotationTest) {
  // After scale and 90 CCW rotation, the new top-left corner would be (0, -10). Translate back to
  // position.
  glm::mat3 matrix = glm::translate(glm::mat3(1.0), glm::vec2(0, 10));
  matrix = glm::rotate(matrix, -glm::half_pi<float>());
  matrix = glm::scale(matrix, glm::vec2(10, 20));

  fuchsia::hardware::display::Frame expected_dst = {
      .x_pos = 0u, .y_pos = 0u, .width = 20u, .height = 10u};

  HardwareFrameCorrectnessWithRotationTester(matrix, expected_dst, fhd_Transform::ROT_90);
}

TEST_F(DisplayCompositorTest, HardwareFrameCorrectnessWith180DegreeRotationTest) {
  // After scale and 180 CCW rotation, the new top-left corner would be (-10, -20). Translate back
  // to position.
  glm::mat3 matrix = glm::translate(glm::mat3(1.0), glm::vec2(10, 20));
  matrix = glm::rotate(matrix, -glm::pi<float>());
  matrix = glm::scale(matrix, glm::vec2(10, 20));

  fuchsia::hardware::display::Frame expected_dst = {
      .x_pos = 0u, .y_pos = 0u, .width = 10u, .height = 20u};

  HardwareFrameCorrectnessWithRotationTester(matrix, expected_dst, fhd_Transform::ROT_180);
}

TEST_F(DisplayCompositorTest, HardwareFrameCorrectnessWith270DegreeRotationTest) {
  // After scale and 270 CCW rotation, the new top-left corner would be (-20, 0). Translate back to
  // position.
  glm::mat3 matrix = glm::translate(glm::mat3(1.0), glm::vec2(20, 0));
  matrix = glm::rotate(matrix, -glm::three_over_two_pi<float>());
  matrix = glm::scale(matrix, glm::vec2(10, 20));

  fuchsia::hardware::display::Frame expected_dst = {
      .x_pos = 0u, .y_pos = 0u, .width = 20u, .height = 10u};

  HardwareFrameCorrectnessWithRotationTester(matrix, expected_dst, fhd_Transform::ROT_270);
}

TEST_F(DisplayCompositorTest, ChecksDisplayImageSignalFences) {
  const uint64_t kGlobalBufferCollectionId = 1;
  auto session = CreateSession();

  // Create the root handle and a handle that will have an image attached.
  const TransformHandle root_handle = session.graph().CreateTransform();
  const TransformHandle image_handle = session.graph().CreateTransform();
  session.graph().AddChild(root_handle, image_handle);

  // Get an UberStruct for the session.
  auto uber_struct = session.CreateUberStructWithCurrentTopology(root_handle);

  // Add an image.
  ImageMetadata image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = 128,
      .height = 256,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };
  uber_struct->images[image_handle] = image_metadata;
  uber_struct->local_matrices[image_handle] =
      glm::scale(glm::translate(glm::mat3(1.0), glm::vec2(9, 13)), glm::vec2(10, 20));
  uber_struct->local_image_sample_regions[image_handle] = {0, 0, 128, 256};

  // Submit the UberStruct.
  session.PushUberStruct(std::move(uber_struct));

  // Set the mock display controller functions and wait for messages.
  auto mock = mock_display_controller_.get();
  std::thread server([&mock]() mutable {
    // Since we have 1 rectangles with image with 1 buffer collection, we have to wait
    // for...:
    // - 2 call for importing and setting constraints on the collection.
    // - 2 call to create layers.
    // - 1 call to import the image.
    // - 1 call to discard the config.
    // - 1 call to set the layers on the display.
    // - 1 call to import event for image.
    // - 1 call to set the layer image.
    // - 1 call to set the layer primary config.
    // - 1 call to set the layer primary alpha.
    // - 1 call to set the layer primary position.
    // - 1 call to check the config.
    // - 1 call to apply the config.
    // - 1 call to GetLatestAppliedConfigStamp
    // - 2 calls to discard the config.
    // - 1 call to discard the config.
    // - 2 calls to destroy layer.
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 21; i++) {
      mock->WaitForMessage();
    }
  });

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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);
  SetDisplaySupported(kGlobalBufferCollectionId, true);

  // Import image.
  EXPECT_CALL(*mock, ImportImage2(_, kGlobalBufferCollectionId, _, 0, _))
      .WillOnce(testing::Invoke(
          [](fuchsia::hardware::display::ImageConfig, uint64_t, uint64_t, uint32_t,
             MockDisplayController::ImportImage2Callback callback) { callback(ZX_OK); }));

  EXPECT_CALL(*renderer_.get(), ImportBufferImage(image_metadata, _)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);

  // We start the frame by clearing the config.
  EXPECT_CALL(*mock, CheckConfig(true, _))
      .WillRepeatedly(
          testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
            fuchsia::hardware::display::ConfigResult result =
                fuchsia::hardware::display::ConfigResult::OK;
            std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
            callback(result, ops);
          }));

  // Set expectation for CreateLayer calls.
  uint64_t layer_id = 1;
  std::vector<uint64_t> layers = {1u, 2u};
  EXPECT_CALL(*mock, CreateLayer(_))
      .Times(2)
      .WillRepeatedly(testing::Invoke([&](MockDisplayController::CreateLayerCallback callback) {
        callback(ZX_OK, layer_id++);
      }));
  EXPECT_CALL(*renderer_.get(), ChoosePreferredPixelFormat(_));

  // Add display.
  uint64_t kDisplayId = 1;
  glm::uvec2 kResolution(1024, 768);
  DisplayInfo display_info = {kResolution, {kPixelFormat}};
  scenic_impl::display::Display display(kDisplayId, kResolution.x, kResolution.y);
  display_compositor_->AddDisplay(&display, display_info, /*num_vmos*/ 0,
                                  /*out_buffer_collection*/ nullptr);

  // Set expectation for rendering image on layer.
  std::vector<uint64_t> active_layers = {1u};
  zx::event imported_event;
  EXPECT_CALL(*mock, ImportEvent(_, _))
      .WillOnce(testing::Invoke(
          [&imported_event](zx::event event, uint64_t) { imported_event = std::move(event); }));
  EXPECT_CALL(*mock, SetDisplayLayers(kDisplayId, active_layers)).Times(1);
  EXPECT_CALL(*mock, SetLayerPrimaryConfig(layers[0], _)).Times(1);
  EXPECT_CALL(*mock, SetLayerPrimaryPosition(layers[0], _, _, _)).Times(1);
  EXPECT_CALL(*mock, SetLayerPrimaryAlpha(layers[0], _, _)).Times(1);
  EXPECT_CALL(*mock, SetLayerImage(layers[0], _, _, _)).Times(1);
  EXPECT_CALL(*mock, CheckConfig(false, _))
      .WillOnce(testing::Invoke([&](bool, MockDisplayController::CheckConfigCallback callback) {
        fuchsia::hardware::display::ConfigResult result =
            fuchsia::hardware::display::ConfigResult::OK;
        std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
        callback(result, ops);
      }));
  EXPECT_CALL(*mock, ApplyConfig()).WillOnce(Return());
  EXPECT_CALL(*mock, GetLatestAppliedConfigStamp(_))
      .WillOnce(
          testing::Invoke([&](MockDisplayController::GetLatestAppliedConfigStampCallback callback) {
            fuchsia::hardware::display::ConfigStamp stamp = {1};
            callback(stamp);
          }));

  // Render image. This should end up in display.
  const auto& display_list =
      GenerateDisplayListForTest({{kDisplayId, {display_info, root_handle}}});
  display_compositor_->RenderFrame(1, zx::time(1), display_list, {},
                                   [](const scheduling::FrameRenderer::Timestamps&) {});

  // Try rendering again. Because |imported_event| isn't signaled and no render targets were created
  // when adding display, we should fail.
  auto status = imported_event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr);
  EXPECT_NE(status, ZX_OK);
  display_compositor_->RenderFrame(1, zx::time(1), display_list, {},
                                   [](const scheduling::FrameRenderer::Timestamps&) {});

  for (uint32_t i = 0; i < 2; i++) {
    EXPECT_CALL(*mock, DestroyLayer(layers[i]));
  }
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
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([&token_ref](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                             fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                             BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token_ref = std::move(token);
        return true;
      });
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              CreateToken(), BufferCollectionUsage::kClientImage,
                                              std::nullopt);

  EXPECT_CALL(*mock_display_controller_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId))
      .WillOnce(Return());
  EXPECT_CALL(*renderer_.get(), ReleaseBufferCollection(kGlobalBufferCollectionId, _))
      .WillOnce(Return());
  display_compositor_->ReleaseBufferCollection(kGlobalBufferCollectionId,
                                               BufferCollectionUsage::kClientImage);

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

class ParameterizedYuvDisplayCompositorTest
    : public DisplayCompositorTest,
      public ::testing::WithParamInterface<fuchsia::sysmem::PixelFormatType> {};

// TODO(fxbug.dev/85601): This test tries to import a YUV buffer to display and confirms that
// Flatland falls back to vulkan compositing. Remove this test when i915 supports YUV buffers.
TEST_P(ParameterizedYuvDisplayCompositorTest, EnforceDisplayConstraints_SkipsYuvImages) {
  SetBufferCollectionImportMode(BufferCollectionImportMode::EnforceDisplayConstraints);

  auto mock = mock_display_controller_.get();
  // Set the mock display controller functions and wait for messages.
  std::thread server([&mock]() mutable {
    // - 1 call to ImportBufferCollection
    // - 1 call to SetBufferCollectionConstraints
    // - 1 call to DiscardConfig
    // TODO(fxbug.dev/71264): Use function call counters from display's MockDisplayController.
    for (uint32_t i = 0; i < 3; i++) {
      mock->WaitForMessage();
    }
  });

  const auto kGlobalBufferCollectionId = allocation::GenerateUniqueBufferCollectionId();

  // Import buffer collection.
  EXPECT_CALL(*mock, ImportBufferCollection(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
             MockDisplayController::ImportBufferCollectionCallback callback) {
            token.BindSync()->Close();
            callback(ZX_OK);
          }));
  EXPECT_CALL(*mock, SetBufferCollectionConstraints(kGlobalBufferCollectionId, _, _))
      .WillOnce(testing::Invoke(
          [](uint64_t collection_id, fuchsia::hardware::display::ImageConfig config,
             MockDisplayController::SetBufferCollectionConstraintsCallback callback) {
            callback(ZX_OK);
          }));
  EXPECT_CALL(*renderer_.get(), ImportBufferCollection(kGlobalBufferCollectionId, _, _, _, _))
      .WillOnce([](allocation::GlobalBufferCollectionId, fuchsia::sysmem::Allocator_Sync*,
                   fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                   BufferCollectionUsage, std::optional<fuchsia::math::SizeU>) {
        token.BindSync()->Close();
        return true;
      });
  auto token = CreateToken();
  EXPECT_TRUE(token.is_valid());
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  auto sync_token = token.BindSync();
  sync_token->Duplicate(ZX_RIGHT_SAME_RIGHTS, dup_token.NewRequest());
  sync_token->Sync();
  display_compositor_->ImportBufferCollection(kGlobalBufferCollectionId, sysmem_allocator_.get(),
                                              std::move(sync_token),
                                              BufferCollectionUsage::kClientImage, std::nullopt);

  // Allocate YUV buffer collection using param.
  const uint32_t kWidth = 128;
  const uint32_t height = 256;
  {
    auto [buffer_usage, memory_constraints] = GetUsageAndMemoryConstraintsForCpuWriteOften();
    fuchsia::sysmem::BufferCollectionSyncPtr texture_collection =
        CreateBufferCollectionSyncPtrAndSetConstraints(
            sysmem_allocator_.get(), std::move(dup_token), 1, kWidth, height, buffer_usage,
            GetParam(), memory_constraints);
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 collection_info;
    auto status = texture_collection->WaitForBuffersAllocated(&allocation_status, &collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    texture_collection->Close();
  }

  // Import image.
  ImageMetadata image_metadata = ImageMetadata{
      .collection_id = kGlobalBufferCollectionId,
      .identifier = allocation::GenerateUniqueImageId(),
      .vmo_index = 0,
      .width = kWidth,
      .height = height,
      .blend_mode = fuchsia::ui::composition::BlendMode::SRC,
  };
  // Make sure the image isn't imported to display.
  EXPECT_CALL(*mock, ImportImage2(_, _, _, _, _)).Times(0);
  EXPECT_CALL(*renderer_.get(), ImportBufferImage(image_metadata, _)).WillOnce(Return(true));
  display_compositor_->ImportBufferImage(image_metadata, BufferCollectionUsage::kClientImage);

  // Shutdown.
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

INSTANTIATE_TEST_SUITE_P(PixelFormats, ParameterizedYuvDisplayCompositorTest,
                         ::testing::Values(fuchsia::sysmem::PixelFormatType::NV12,
                                           fuchsia::sysmem::PixelFormatType::I420));

}  // namespace test
}  // namespace flatland
