// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screen_capture2/screen_capture2.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/sys/cpp/testing/component_context_provider.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/ui/composition/internal/cpp/fidl.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/buffers/buffer_collection.h"
#include "src/ui/scenic/lib/flatland/renderer/mock_renderer.h"
#include "src/ui/scenic/lib/flatland/renderer/null_renderer.h"
#include "src/ui/scenic/lib/screen_capture/screen_capture_buffer_collection_importer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using allocation::Allocator;
using allocation::BufferCollectionImporter;
using fuchsia::ui::composition::RegisterBufferCollectionArgs;
using fuchsia::ui::composition::RegisterBufferCollectionUsage;
using fuchsia::ui::composition::internal::FrameInfo;
using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;
using screen_capture::ScreenCaptureBufferCollectionImporter;
using testing::_;

namespace screen_capture2 {
namespace test {

class ScreenCapture2Test : public gtest::TestLoopFixture {
 public:
  ScreenCapture2Test() = default;
  void SetUp() override {
    // Create the SysmemAllocator.
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr();

    renderer_ = std::make_shared<flatland::NullRenderer>();
    importer_ = std::make_unique<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("ScreenCapture2Test"), renderer_,
        /*enable_copy_fallback=*/false);

    renderables_ =
        std::make_pair<std::vector<Rectangle2D>, std::vector<allocation::ImageMetadata>>({}, {});
  }

  void SetUpMockImporter() {
    mock_renderer_ = std::make_shared<flatland::MockRenderer>();
    importer_ = std::make_unique<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("ScreenCapture2Test"), mock_renderer_,
        /*enable_copy_fallback=*/false);
  }

  std::shared_ptr<Allocator> CreateAllocator() {
    std::vector<std::shared_ptr<BufferCollectionImporter>> extra_importers;
    std::vector<std::shared_ptr<BufferCollectionImporter>> screenshot_importers;
    screenshot_importers.push_back(importer_);
    return std::make_shared<Allocator>(context_provider_.context(), extra_importers,
                                       screenshot_importers,
                                       utils::CreateSysmemAllocatorSyncPtr("-allocator"));
  }

  // Configures ScreenCapture with given args successfully.
  void SetUpScreenCapture(screen_capture2::ScreenCapture& sc, BufferCount buffer_count,
                          uint32_t image_width, uint32_t image_height, bool is_mock) {
    flatland::BufferCollectionInfo buffer_info;
    if (is_mock) {
      EXPECT_CALL(*mock_renderer_.get(), RegisterRenderTargetCollection(_, _, _, _))
          .WillRepeatedly([&buffer_info](
                              allocation::GlobalBufferCollectionId collection_id,
                              fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                              fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                              fuchsia::math::SizeU size = {}) {
            auto result = flatland::BufferCollectionInfo::New(sysmem_allocator, std::move(token));

            if (result.is_error()) {
              FX_LOGS(WARNING) << "Unable to register collection.";
              return false;
            }
            buffer_info = std::move(result.value());
            return true;
          });

      EXPECT_CALL(*mock_renderer_.get(), ImportBufferImage(_))
          .WillRepeatedly(testing::Return(true));

      EXPECT_CALL(*mock_renderer_.get(), Render(_, _, _, _, _))
          .WillRepeatedly([](const allocation::ImageMetadata& render_target,
                             const std::vector<Rectangle2D>& rectangles,
                             const std::vector<allocation::ImageMetadata>& images,
                             const std::vector<zx::event>& release_fences,
                             bool apply_color_conversion) {
            // Fire all of the release fences.
            for (auto& fence : release_fences) {
              fence.signal(0, ZX_EVENT_SIGNALED);
            }
          });

      EXPECT_CALL(*mock_renderer_.get(), DeregisterRenderTargetCollection(_))
          .Times(::testing::AtLeast(0));
    }

    allocation::BufferCollectionImportExportTokens ref_pair =
        allocation::BufferCollectionImportExportTokens::New();

    std::shared_ptr<Allocator> flatland_allocator = CreateAllocator();
    CreateBufferCollectionInfo2WithConstraints(
        utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
        std::move(ref_pair.export_token), flatland_allocator);

    ScreenCaptureConfig args;
    args.set_import_token(std::move(ref_pair.import_token));
    args.set_image_size({image_width, image_height});

    fpromise::result<void, ScreenCaptureError> configure_result;
    sc.Configure(std::move(args),
                 [&configure_result](fpromise::result<void, ScreenCaptureError> result) {
                   EXPECT_FALSE(result.is_error());
                   configure_result = result;
                 });
    RunLoopUntilIdle();
    EXPECT_TRUE(configure_result.is_ok());
  }

  void CreateBufferCollectionInfo2WithConstraints(
      fuchsia::sysmem::BufferCollectionConstraints constraints,
      allocation::BufferCollectionExportToken export_token,
      std::shared_ptr<Allocator> flatland_allocator) {
    RegisterBufferCollectionArgs rbc_args = {};

    zx_status_t status;
    // Create Sysmem tokens.
    auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator_.get());

    rbc_args.set_export_token(std::move(export_token));
    rbc_args.set_buffer_collection_token(std::move(dup_token));
    rbc_args.set_usage(RegisterBufferCollectionUsage::SCREENSHOT);

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    status = sysmem_allocator_->BindSharedCollection(std::move(local_token),
                                                     buffer_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);

    status = buffer_collection->SetConstraints(true, constraints);
    EXPECT_EQ(status, ZX_OK);

    bool processed_callback = false;
    flatland_allocator->RegisterBufferCollection(
        std::move(rbc_args),
        [&processed_callback](
            fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result result) {
          EXPECT_EQ(false, result.is_err());
          processed_callback = true;
        });

    // Wait for allocation.
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    ASSERT_EQ(ZX_OK, status);
    ASSERT_EQ(ZX_OK, allocation_status);
    ASSERT_EQ(constraints.min_buffer_count, buffer_collection_info.buffer_count);

    buffer_collection->Close();
  }

  std::pair<std::vector<Rectangle2D>, std::vector<allocation::ImageMetadata>> GetRenderables() {
    return renderables_;
  }

  bool GetReceivedLastFrame(screen_capture2::ScreenCapture& sc) {
    return sc.get_client_received_last_frame();
  }

 protected:
  std::shared_ptr<flatland::NullRenderer> renderer_;
  std::shared_ptr<flatland::MockRenderer> mock_renderer_;
  std::shared_ptr<ScreenCaptureBufferCollectionImporter> importer_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  sys::testing::ComponentContextProvider context_provider_;

 private:
  std::pair<std::vector<Rectangle2D>, std::vector<allocation::ImageMetadata>> renderables_;
};

TEST_F(ScreenCapture2Test, ConfigureWithMissingArguments) {
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, nullptr,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  std::shared_ptr<Allocator> flatland_allocator = CreateAllocator();
  CreateBufferCollectionInfo2WithConstraints(
      utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
      std::move(ref_pair.export_token), flatland_allocator);

  // Missing image size.
  {
    ScreenCaptureConfig args;
    args.set_import_token(std::move(ref_pair.import_token));

    ScreenCaptureError error;
    sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
      EXPECT_TRUE(result.is_error());
      error = result.error();
    });
    RunLoopUntilIdle();
    EXPECT_EQ(error, ScreenCaptureError::MISSING_ARGS);
  }

  // Missing import token.
  {
    ScreenCaptureConfig args;
    args.set_image_size({image_width, image_height});

    ScreenCaptureError error;
    sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
      EXPECT_TRUE(result.is_error());
      error = result.error();
    });
    RunLoopUntilIdle();
    EXPECT_EQ(error, ScreenCaptureError::MISSING_ARGS);
  }

  // Unable to get buffer count.
  {
    allocation::BufferCollectionImportExportTokens ref_pair2 =
        allocation::BufferCollectionImportExportTokens::New();

    ScreenCaptureConfig args;
    args.set_import_token(std::move(ref_pair2.import_token));
    args.set_image_size({image_width, image_height});

    ScreenCaptureError error;
    sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
      EXPECT_TRUE(result.is_error());
      error = result.error();
    });
    RunLoopUntilIdle();
    EXPECT_EQ(error, ScreenCaptureError::INVALID_ARGS);
  }

  // Has invalid import token.
  {
    allocation::BufferCollectionImportExportTokens ref_pair2 =
        allocation::BufferCollectionImportExportTokens::New();
    ref_pair2.import_token.value.reset();

    ScreenCaptureConfig args;
    args.set_import_token(std::move(ref_pair2.import_token));
    args.set_image_size({image_width, image_height});

    ScreenCaptureError error;
    sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
      EXPECT_TRUE(result.is_error());
      error = result.error();
    });
    RunLoopUntilIdle();
    EXPECT_EQ(error, ScreenCaptureError::INVALID_ARGS);
  }
}

// The test uses a mock to fail ImportBufferImage() at a specific buffer during Configure()
// and ensure ReleaseBufferImage gets called the correct number of times.
TEST_F(ScreenCapture2Test, Configure_BufferCollectionFailure) {
  SetUpMockImporter();
  flatland::BufferCollectionInfo buffer_info;
  EXPECT_CALL(*mock_renderer_.get(), RegisterRenderTargetCollection(_, _, _, _))
      .WillRepeatedly(
          [&buffer_info](allocation::GlobalBufferCollectionId collection_id,
                         fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                         fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                         fuchsia::math::SizeU size = {}) {
            auto result = flatland::BufferCollectionInfo::New(sysmem_allocator, std::move(token));

            if (result.is_error()) {
              FX_LOGS(WARNING) << "Unable to register collection.";
              return false;
            }
            buffer_info = std::move(result.value());
            return true;
          });

  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, nullptr,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 3;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  std::shared_ptr<Allocator> flatland_allocator = CreateAllocator();
  CreateBufferCollectionInfo2WithConstraints(
      utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
      std::move(ref_pair.export_token), flatland_allocator);

  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_image_size({image_width, image_height});

  EXPECT_CALL(*mock_renderer_.get(), ImportBufferImage(_))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(false));

  EXPECT_CALL(*mock_renderer_.get(), ReleaseBufferImage(_)).Times(buffer_count - 1);

  ScreenCaptureError error;
  sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    error = result.error();
  });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenCaptureError::INVALID_ARGS);

  // Expect all BufferImages are released before any tear down of test.
  EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(0);
}

TEST_F(ScreenCapture2Test, Configure_Success) {
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, false);
}

TEST_F(ScreenCapture2Test, GetNextFrame_Success) {
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, false);

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
  sc.GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result = std::move(result);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(gnf_result.is_ok());
}

// Releasing buffer after first render allows it to be reused during successive call.
TEST_F(ScreenCapture2Test, GetNextFrame_SuccessiveCallSuccess) {
  SetUpMockImporter();
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, mock_renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, true);

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
  sc.GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result = std::move(result);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(gnf_result.is_ok());
  FrameInfo info = std::move(gnf_result.value());

  EXPECT_TRUE(GetReceivedLastFrame(sc));

  zx::eventpair token = std::move(*info.mutable_buffer_release_token());
  EXPECT_EQ(token.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
  RunLoopUntilIdle();

  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result2;
  sc.GetNextFrame([&gnf_result2](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result2 = std::move(result);
  });
  RunLoopUntilIdle();

  // Since |recieved_last_frame_| is true, GetNextFrame() will be hanging.
  sc.RenderFrame();
  RunLoopUntilIdle();

  EXPECT_TRUE(gnf_result2.is_ok());
  FrameInfo info2 = std::move(gnf_result2.value());
  EXPECT_EQ(info2.buffer_index(), info.buffer_index());

  EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCapture2Test, GetNextFrame_Errors) {
  SetUpMockImporter();
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, mock_renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, true);

  // Overwriting hanging get.
  {
    fpromise::result<FrameInfo, ScreenCaptureError> gnf_result1;
    sc.GetNextFrame([&gnf_result1](fpromise::result<FrameInfo, ScreenCaptureError> result) {
      EXPECT_FALSE(result.is_error());
      gnf_result1 = std::move(result);
    });
    ScreenCaptureError error;
    sc.GetNextFrame([&error](fpromise::result<FrameInfo, ScreenCaptureError> result) {
      EXPECT_TRUE(result.is_error());
      error = result.error();
    });
    RunLoopUntilIdle();
    EXPECT_TRUE(gnf_result1.is_ok());
    EXPECT_EQ(error, ScreenCaptureError::BAD_HANGING_GET);

    EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(1);
  }
}

// Releasing buffer while client has been waiting immediately renders the frame.
TEST_F(ScreenCapture2Test, GetNextFrame_BuffersFull) {
  SetUpMockImporter();
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, mock_renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, true);

  // Makes buffer unavailable.
  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
  sc.GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    gnf_result = std::move(result);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(gnf_result.is_ok());
  FrameInfo info = std::move(gnf_result.value());

  zx::eventpair token = std::move(*info.mutable_buffer_release_token());

  bool callback_called = false;
  fpromise::result<FrameInfo, ScreenCaptureError> gnf_result2;
  sc.GetNextFrame(
      [&gnf_result2, &callback_called](fpromise::result<FrameInfo, ScreenCaptureError> result) {
        EXPECT_FALSE(result.is_error());
        gnf_result2 = std::move(result);
        callback_called = true;
      });
  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);

  EXPECT_EQ(token.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
  RunLoopUntilIdle();

  // Since |received_last_frame_| is true, GetNextFrame will be hanging.
  sc.RenderFrame();
  RunLoopUntilIdle();

  EXPECT_TRUE(gnf_result2.is_ok());
  EXPECT_TRUE(callback_called);
  FrameInfo info2 = std::move(gnf_result2.value());
  EXPECT_EQ(info2.buffer_index(), info.buffer_index());

  EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCapture2Test, RenderFrame_Errors) {
  SetUpMockImporter();
  fuchsia::ui::composition::internal::ScreenCapturePtr screencapturer;
  screen_capture2::ScreenCapture sc(screencapturer.NewRequest(), importer_, mock_renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, true);

  // |available_buffers_| is empty.
  {
    fpromise::result<FrameInfo, ScreenCaptureError> gnf_result;
    sc.GetNextFrame([&gnf_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
      EXPECT_FALSE(result.is_error());
      gnf_result = std::move(result);
    });
    RunLoopUntilIdle();
    EXPECT_TRUE(gnf_result.is_ok());
    EXPECT_TRUE(GetReceivedLastFrame(sc));

    bool callback_called = false;
    sc.GetNextFrame([&callback_called](fpromise::result<FrameInfo, ScreenCaptureError> result) {
      callback_called = true;
    });
    RunLoopUntilIdle();
    sc.RenderFrame();
    RunLoopUntilIdle();
    EXPECT_FALSE(callback_called);
    EXPECT_FALSE(GetReceivedLastFrame(sc));

    // Clean up test.
    FrameInfo info = std::move(gnf_result.value());
    zx::eventpair token = std::move(*info.mutable_buffer_release_token());
    EXPECT_EQ(token.signal_peer(0, ZX_EVENTPAIR_SIGNALED), ZX_OK);
    RunLoopUntilIdle();

    EXPECT_TRUE(GetReceivedLastFrame(sc));
  }

  // |current_callback_| does not exist.
  {
    EXPECT_TRUE(GetReceivedLastFrame(sc));

    sc.RenderFrame();
    RunLoopUntilIdle();
    EXPECT_FALSE(GetReceivedLastFrame(sc));
  }
}

}  // namespace test
}  // namespace screen_capture2
