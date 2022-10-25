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
#include "src/ui/scenic/lib/screen_capture2/tests/common.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using allocation::Allocator;
using allocation::BufferCollectionImporter;
using flatland::ImageRect;
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
    importer_ = std::make_shared<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("ScreenCapture2Test"), renderer_,
        /*enable_copy_fallback=*/false);

    renderables_ =
        std::make_pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>>({}, {});
  }

  void SetUpMockImporter() {
    mock_renderer_ = std::make_shared<flatland::MockRenderer>();
    importer_ = std::make_shared<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("ScreenCapture2Test"), mock_renderer_,
        /*enable_copy_fallback=*/false);
  }

  // Configures ScreenCapture with given args successfully.
  void SetUpScreenCapture(screen_capture2::ScreenCapture& sc, BufferCount buffer_count,
                          uint32_t image_width, uint32_t image_height, bool is_mock) {
    flatland::BufferCollectionInfo buffer_info;
    if (is_mock) {
      EXPECT_CALL(*mock_renderer_.get(), ImportBufferCollection(_, _, _, _, _))
          .WillRepeatedly([&buffer_info](
                              allocation::GlobalBufferCollectionId collection_id,
                              fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                              fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                              allocation::BufferCollectionUsage,
                              std::optional<fuchsia::math::SizeU> size) {
            auto result = flatland::BufferCollectionInfo::New(sysmem_allocator, std::move(token));

            if (result.is_error()) {
              FX_LOGS(WARNING) << "Unable to register collection.";
              return false;
            }
            buffer_info = std::move(result.value());
            return true;
          });

      EXPECT_CALL(*mock_renderer_.get(), ImportBufferImage(_, _))
          .WillRepeatedly(testing::Return(true));

      EXPECT_CALL(*mock_renderer_.get(), Render(_, _, _, _, _))
          .WillRepeatedly([](const allocation::ImageMetadata& render_target,
                             const std::vector<ImageRect>& rectangles,
                             const std::vector<allocation::ImageMetadata>& images,
                             const std::vector<zx::event>& release_fences,
                             bool apply_color_conversion) {
            // Fire all of the release fences.
            for (auto& fence : release_fences) {
              fence.signal(0, ZX_EVENT_SIGNALED);
            }
          });

      EXPECT_CALL(*mock_renderer_.get(), ReleaseBufferCollection(_, _))
          .Times(::testing::AtLeast(0));
    }

    allocation::BufferCollectionImportExportTokens ref_pair =
        allocation::BufferCollectionImportExportTokens::New();

    std::shared_ptr<Allocator> flatland_allocator =
        CreateAllocator(importer_, context_provider_.context());
    CreateBufferCollectionInfo2WithConstraints(
        utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
        std::move(ref_pair.export_token), flatland_allocator, sysmem_allocator_.get());

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

  std::pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>> GetRenderables() {
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
  std::pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>> renderables_;
};

TEST_F(ScreenCapture2Test, ConfigureWithMissingArguments) {
  screen_capture2::ScreenCapture sc(importer_, nullptr,
                                    [this]() { return this->GetRenderables(); });

  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  std::shared_ptr<Allocator> flatland_allocator =
      CreateAllocator(importer_, context_provider_.context());
  CreateBufferCollectionInfo2WithConstraints(
      utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
      std::move(ref_pair.export_token), flatland_allocator, sysmem_allocator_.get());

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
  EXPECT_CALL(*mock_renderer_.get(), ImportBufferCollection(_, _, _, _, _))
      .WillRepeatedly(
          [&buffer_info](allocation::GlobalBufferCollectionId collection_id,
                         fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                         fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                         allocation::BufferCollectionUsage,
                         std::optional<fuchsia::math::SizeU> size) {
            auto result = flatland::BufferCollectionInfo::New(sysmem_allocator, std::move(token));

            if (result.is_error()) {
              FX_LOGS(WARNING) << "Unable to register collection.";
              return false;
            }
            buffer_info = std::move(result.value());
            return true;
          });

  screen_capture2::ScreenCapture sc(importer_, nullptr,
                                    [this]() { return this->GetRenderables(); });

  const BufferCount buffer_count = 3;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  std::shared_ptr<Allocator> flatland_allocator =
      CreateAllocator(importer_, context_provider_.context());
  CreateBufferCollectionInfo2WithConstraints(
      utils::CreateDefaultConstraints(buffer_count, image_width, image_height),
      std::move(ref_pair.export_token), flatland_allocator, sysmem_allocator_.get());

  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_image_size({image_width, image_height});

  EXPECT_CALL(*mock_renderer_.get(), ImportBufferImage(_, _))
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
  screen_capture2::ScreenCapture sc(importer_, renderer_,
                                    [this]() { return this->GetRenderables(); });
  const BufferCount buffer_count = 1;
  const uint32_t image_width = 1;
  const uint32_t image_height = 1;

  SetUpScreenCapture(sc, buffer_count, image_width, image_height, false);
}

TEST_F(ScreenCapture2Test, GetNextFrame_Success) {
  screen_capture2::ScreenCapture sc(importer_, renderer_,
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
  screen_capture2::ScreenCapture sc(importer_, mock_renderer_,
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
  sc.MaybeRenderFrame();
  RunLoopUntilIdle();

  EXPECT_TRUE(gnf_result2.is_ok());
  FrameInfo info2 = std::move(gnf_result2.value());
  EXPECT_EQ(info2.buffer_index(), info.buffer_index());

  EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCapture2Test, GetNextFrame_Errors) {
  SetUpMockImporter();
  screen_capture2::ScreenCapture sc(importer_, mock_renderer_,
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
  screen_capture2::ScreenCapture sc(importer_, mock_renderer_,
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
  sc.MaybeRenderFrame();
  RunLoopUntilIdle();

  EXPECT_TRUE(gnf_result2.is_ok());
  EXPECT_TRUE(callback_called);
  FrameInfo info2 = std::move(gnf_result2.value());
  EXPECT_EQ(info2.buffer_index(), info.buffer_index());

  EXPECT_CALL(*mock_renderer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCapture2Test, MaybeRenderFrame_Errors) {
  SetUpMockImporter();
  screen_capture2::ScreenCapture sc(importer_, mock_renderer_,
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
    sc.MaybeRenderFrame();
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

    sc.MaybeRenderFrame();
    RunLoopUntilIdle();
    EXPECT_FALSE(GetReceivedLastFrame(sc));
  }
}

}  // namespace test
}  // namespace screen_capture2
