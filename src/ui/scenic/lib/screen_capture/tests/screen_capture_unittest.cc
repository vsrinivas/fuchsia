// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screen_capture/screen_capture.h"

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/mock_buffer_collection_importer.h"
#include "src/ui/scenic/lib/flatland/renderer/mock_renderer.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using allocation::BufferCollectionImporter;
using flatland::ImageRect;
using fuchsia::ui::composition::FrameInfo;
using fuchsia::ui::composition::GetNextFrameArgs;
using fuchsia::ui::composition::ScreenCaptureConfig;
using fuchsia::ui::composition::ScreenCaptureError;
using testing::_;

namespace screen_capture::test {

class ScreenCaptureTest : public gtest::TestLoopFixture {
 public:
  ScreenCaptureTest() = default;
  void SetUp() override {
    sysmem_allocator_ = utils::CreateSysmemAllocatorSyncPtr("ScreenCaptureTest::Setup");

    mock_buffer_collection_importer_ = new allocation::MockBufferCollectionImporter();
    buffer_collection_importer_ =
        std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer_);

    renderer_ = std::make_shared<flatland::MockRenderer>();
    renderables_ =
        std::make_pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>>({}, {});

    // Capture uninteresting cleanup calls from Allocator dtor.
    EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferCollection(_, _))
        .Times(::testing::AtLeast(0));
  }

  void TearDown() override { RunLoopUntilIdle(); }

  fpromise::result<FrameInfo, ScreenCaptureError> CaptureScreen(screen_capture::ScreenCapture& sc) {
    GetNextFrameArgs frame_args;
    zx::event event;
    zx::event::create(0, &event);
    frame_args.set_event(std::move(event));

    fpromise::result<FrameInfo, ScreenCaptureError> response;
    bool alloc_result = false;
    sc.GetNextFrame(
        std::move(frame_args),
        [&response, &alloc_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
          response = std::move(result);
          alloc_result = true;
        });
    RunLoopUntilIdle();
    EXPECT_TRUE(alloc_result);
    return response;
  }

  std::pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>> GetRenderables() {
    return renderables_;
  }

 protected:
  allocation::MockBufferCollectionImporter* mock_buffer_collection_importer_;
  std::shared_ptr<allocation::BufferCollectionImporter> buffer_collection_importer_;
  std::shared_ptr<flatland::MockRenderer> renderer_;

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  std::pair<std::vector<ImageRect>, std::vector<allocation::ImageMetadata>> renderables_;
};

TEST_F(ScreenCaptureTest, ConfigureSingleImporterSuccess) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, ConfigureSingleImporterFailure) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(false));

  ScreenCaptureError error;
  sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    error = result.error();
  });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenCaptureError::BAD_OPERATION);
}

TEST_F(ScreenCaptureTest, ConfigureMultipleImportersSuccess) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;

  auto mock_buffer_collection_importer2 = new allocation::MockBufferCollectionImporter();
  auto buffer_collection_importer2 =
      std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer2);

  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture_importers.push_back(buffer_collection_importer2);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));
  EXPECT_CALL(*mock_buffer_collection_importer2, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // We expect that for each buffer importer the buffer image will be released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
  EXPECT_CALL(*mock_buffer_collection_importer2, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, ConfigureMultipleImportersImportFailure) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;

  auto mock_buffer_collection_importer2 = new allocation::MockBufferCollectionImporter();
  auto buffer_collection_importer2 =
      std::shared_ptr<allocation::BufferCollectionImporter>(mock_buffer_collection_importer2);

  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture_importers.push_back(buffer_collection_importer2);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(3);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(true));
  EXPECT_CALL(*mock_buffer_collection_importer2, ImportBufferImage(_, _))
      .WillOnce(testing::Return(true))
      .WillOnce(testing::Return(false));

  // We expect that all buffer images up to the failure will be released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(2);
  EXPECT_CALL(*mock_buffer_collection_importer2, ReleaseBufferImage(_)).Times(1);

  ScreenCaptureError error;
  sc.Configure(std::move(args), [&error](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    error = result.error();
  });
  RunLoopUntilIdle();
  EXPECT_EQ(error, ScreenCaptureError::BAD_OPERATION);

  // We expect that all buffer images up to the failure will be released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(testing::Exactly(0));
  EXPECT_CALL(*mock_buffer_collection_importer2, ReleaseBufferImage(_)).Times(testing::Exactly(0));
}

TEST_F(ScreenCaptureTest, ConfigureWithMissingArguments) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), {}, nullptr,
                                   [this]() { return this->GetRenderables(); });
  sc.Configure({}, [](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), ScreenCaptureError::MISSING_ARGS);
  });
  RunLoopUntilIdle();
}

TEST_F(ScreenCaptureTest, ConfigureNoBuffers) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(0);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  sc.Configure(std::move(args), [](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_TRUE(result.is_error());
    EXPECT_EQ(result.error(), ScreenCaptureError::INVALID_ARGS);
  });
  RunLoopUntilIdle();
}

TEST_F(ScreenCaptureTest, ConfigureTwice) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  // Configure a buffer collection with two (2) VMOs to render into for GetNextFrame().
  allocation::BufferCollectionImportExportTokens ref_pair1 =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args1;
  args1.set_import_token(std::move(ref_pair1.import_token));
  args1.set_size({1, 1});
  args1.set_buffer_count(2);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool alloc_result = false;
  sc.Configure(std::move(args1),
               [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                 EXPECT_FALSE(result.is_error());
                 alloc_result = true;
               });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Request a frame in the buffer.
  const auto& response_buffer1 = CaptureScreen(sc);
  EXPECT_TRUE(response_buffer1.is_ok());

  // Create another buffer collection to render into for GetNextFrame(). This collection only has
  // one (1) VMO.
  allocation::BufferCollectionImportExportTokens ref_pair2 =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args2;
  args2.set_import_token(std::move(ref_pair2.import_token));
  args2.set_size({1, 1});
  args2.set_buffer_count(1);

  // We expect that the two images created for the buffer collection will be released when we
  // configure our new buffer collection.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(2);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  alloc_result = false;
  sc.Configure(std::move(args2),
               [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                 EXPECT_FALSE(result.is_error());
                 alloc_result = true;
               });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Request a frame into new buffer.
  const auto& response1_buffer2 = CaptureScreen(sc);
  EXPECT_TRUE(response1_buffer2.is_ok());

  // As the new buffer we configured only contained 1 VMO we expect a buffer full error
  // when we request another frame without freeing the first one we got.
  const auto& response2_buffer2 = CaptureScreen(sc);
  EXPECT_TRUE(response2_buffer2.is_error());
  EXPECT_EQ(response2_buffer2.error(), ScreenCaptureError::BUFFER_FULL);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, GetNextFrameNoBuffers) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  // Request a frame without configuring the buffers.
  const auto& result = CaptureScreen(sc);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ScreenCaptureError::BAD_OPERATION);
}

TEST_F(ScreenCaptureTest, GetNextFrameSuccess) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  const auto& result = CaptureScreen(sc);
  EXPECT_TRUE(result.is_ok());

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, GetNextFrameBufferFullError) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Request frame. This will use up the only buffer in the collection.
  const auto& result1 = CaptureScreen(sc);
  EXPECT_TRUE(result1.is_ok());

  // Request another frame. This should cause a buffer full error.
  const auto& result2 = CaptureScreen(sc);
  EXPECT_TRUE(result2.is_error());
  EXPECT_EQ(result2.error(), ScreenCaptureError::BUFFER_FULL);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, GetNextFrameMultipleBuffers) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(2);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Request frame.
  const auto& result1 = CaptureScreen(sc);
  EXPECT_TRUE(result1.is_ok());

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Request another frame. This should fill the second buffer.
  const auto& result2 = CaptureScreen(sc);
  EXPECT_TRUE(result2.is_ok());
  EXPECT_NE(result1.value().buffer_id(), result2.value().buffer_id());

  // Ensure that the buffer images are released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(2);
}

TEST_F(ScreenCaptureTest, GetNextFrameMissingArgs) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, nullptr,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  GetNextFrameArgs frame_args;

  // Request a frame. We have not set the frame args so we expect an error.
  bool alloc_result = false;
  sc.GetNextFrame(std::move(frame_args),
                  [&alloc_result](fpromise::result<FrameInfo, ScreenCaptureError> result) {
                    EXPECT_TRUE(result.is_error());
                    EXPECT_EQ(result.error(), ScreenCaptureError::MISSING_ARGS);
                    alloc_result = true;
                  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, ReleaseAvailableFrame) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool alloc_result = false;
  sc.Configure(std::move(args), [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    alloc_result = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Attempt to release a frame that is not used.
  alloc_result = false;
  sc.ReleaseFrame(/*buffer_id=*/0,
                  [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                    EXPECT_TRUE(result.is_error());
                    EXPECT_EQ(result.error(), ScreenCaptureError::INVALID_ARGS);
                    alloc_result = true;
                  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, ReleaseOutOfRangeFrame) {
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(1);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool alloc_result = false;
  sc.Configure(std::move(args), [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    alloc_result = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Attempt to release an index that is not within the range of valid indices for the buffer
  // collection.
  alloc_result = false;
  sc.ReleaseFrame(/*buffer_id=*/1,
                  [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                    EXPECT_TRUE(result.is_error());
                    EXPECT_EQ(result.error(), ScreenCaptureError::INVALID_ARGS);
                    alloc_result = true;
                  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Ensure that the buffer image is released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(1);
}

TEST_F(ScreenCaptureTest, ReleaseFrameFromFullBuffer) {
  const uint32_t kNumBuffers = 3;
  const uint32_t kFreedBufferId = 1;
  fuchsia::ui::composition::ScreenCapturePtr screencapturer;
  std::vector<std::shared_ptr<BufferCollectionImporter>> screen_capture_importers;
  screen_capture_importers.push_back(buffer_collection_importer_);
  screen_capture::ScreenCapture sc(screencapturer.NewRequest(), screen_capture_importers, renderer_,
                                   [this]() { return this->GetRenderables(); });

  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();
  ScreenCaptureConfig args;
  args.set_import_token(std::move(ref_pair.import_token));
  args.set_size({1, 1});
  args.set_buffer_count(kNumBuffers);

  EXPECT_CALL(*mock_buffer_collection_importer_, ImportBufferImage(_, _))
      .WillRepeatedly(testing::Return(true));

  bool configure = false;
  sc.Configure(std::move(args), [&configure](fpromise::result<void, ScreenCaptureError> result) {
    EXPECT_FALSE(result.is_error());
    configure = true;
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(configure);

  // Fill buffers.
  for (uint32_t i = 0; i < kNumBuffers; i++) {
    // Capture call to renderer.
    EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);
    const auto& result = CaptureScreen(sc);
    EXPECT_TRUE(result.is_ok());
  }

  // Capture screen again without freeing a buffer. This should cause an error.
  const auto& result_full = CaptureScreen(sc);
  EXPECT_TRUE(result_full.is_error());
  EXPECT_EQ(result_full.error(), ScreenCaptureError::BUFFER_FULL);

  // Release a buffer. After this we can call GetNextFrame() successfully again, and it should
  // fill the buffer we released.
  bool alloc_result = false;
  sc.ReleaseFrame(/*buffer_id=*/kFreedBufferId,
                  [&alloc_result](fpromise::result<void, ScreenCaptureError> result) {
                    EXPECT_TRUE(result.is_ok());
                    alloc_result = true;
                  });
  RunLoopUntilIdle();
  EXPECT_TRUE(alloc_result);

  // Capture call to renderer.
  EXPECT_CALL(*renderer_, Render(_, _, _, _, _)).Times(1);

  // Now we request another frame. This should be rendered to the VMO at the index we just released.
  const auto& result_after_release = CaptureScreen(sc);
  EXPECT_TRUE(result_after_release.is_ok());
  EXPECT_EQ(kFreedBufferId, result_after_release.value().buffer_id());

  // Ensure that the buffer images are released.
  EXPECT_CALL(*mock_buffer_collection_importer_, ReleaseBufferImage(_)).Times(3);
}

}  // namespace screen_capture::test
