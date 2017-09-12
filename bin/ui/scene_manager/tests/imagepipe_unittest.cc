// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/image_utils.h"
#include "gtest/gtest.h"

#include "lib/ui/scenic/fidl_helpers.h"
#include "lib/ui/tests/test_with_message_loop.h"
#include "garnet/bin/ui/scene_manager/acquire_fence.h"
#include "garnet/bin/ui/scene_manager/fence.h"
#include "garnet/bin/ui/scene_manager/resources/image_pipe.h"
#include "garnet/bin/ui/scene_manager/tests/mocks.h"
#include "garnet/bin/ui/scene_manager/tests/session_test.h"
#include "garnet/bin/ui/scene_manager/tests/util.h"

namespace scene_manager {
namespace test {

class ImagePipeTest : public SessionTest, public escher::ResourceManager {
 public:
  ImagePipeTest()
      : escher::ResourceManager(nullptr), command_buffer_sequencer_() {}

  std::unique_ptr<Engine> CreateEngine() override {
    auto r = std::make_unique<ReleaseFenceSignallerForTest>(
        &command_buffer_sequencer_);
    mock_release_fence_signaller_ = r.get();
    return std::make_unique<EngineForTest>(&display_manager_, std::move(r));
  }

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}

  DisplayManager display_manager_;
  escher::impl::CommandBufferSequencer command_buffer_sequencer_;
  ReleaseFenceSignallerForTest* mock_release_fence_signaller_;
};

TEST_F(ImagePipeTest, SimpleAcquireFenceSignalling) {
  // Create an AcquireFence.
  mx::event fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence1));
  AcquireFence buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.ready());
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));

  // Still should not be ready.
  EXPECT_FALSE(buffer_fence1.ready());

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  // Expect that it is signalled now.
  EXPECT_TRUE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_TRUE(buffer_fence1.ready());
}

TEST_F(ImagePipeTest, AsyncAcquireFenceSignalling) {
  // Create an AcquireFence.
  mx::event fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &fence1));
  AcquireFence buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.WaitReady(fxl::TimeDelta::Zero()));
  EXPECT_FALSE(buffer_fence1.ready());

  bool signalled = false;
  // Expect that it is signalled now.
  buffer_fence1.WaitReadyAsync([&signalled]() { signalled = true; });

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  RUN_MESSAGE_LOOP_UNTIL(buffer_fence1.ready());
  EXPECT_TRUE(signalled);
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithBuffer(
    size_t buffer_size,
    std::unique_ptr<uint8_t[]> buffer_pixels) {
  auto shared_vmo = CreateSharedVmo(buffer_size);

  memcpy(shared_vmo->Map(), buffer_pixels.get(), buffer_size);
  return shared_vmo;
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithCheckerboardPixels(size_t w,
                                                            size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewCheckerboardPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithGradientPixels(size_t w, size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewGradientPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

class ImagePipeThatCreatesDummyImages : public ImagePipe {
 public:
  ImagePipeThatCreatesDummyImages(
      Session* session,
      escher::ResourceManager* dummy_resource_manager)
      : ImagePipe(session, 0u),
        dummy_resource_manager_(dummy_resource_manager) {}

 private:
  // Override to create an Image without a backing escher::Image.
  ImagePtr CreateImage(Session* session,
                       MemoryPtr memory,
                       const scenic::ImageInfoPtr& image_info,
                       uint64_t memory_offset,
                       ErrorReporter* error_reporter) override {
    return Image::NewForTesting(session, 0u, dummy_resource_manager_, memory);
  }
  escher::ResourceManager* dummy_resource_manager_;
};

// Present two frames on the ImagePipe, making sure that acquire fence is being
// listened to and release fences are signalled.
TEST_F(ImagePipeTest, ImagePipeImageIdMustNotBeZero) {
  ImagePipePtr image_pipe =
      fxl::MakeRefCounted<ImagePipeThatCreatesDummyImages>(session_.get(),
                                                           this);

  uint32_t imageId1 = 0;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);

    auto image_info = scenic::ImageInfo::New();
    image_info->pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
    image_info->tiling = scenic::ImageInfo::Tiling::LINEAR;
    image_info->width = image_dim;
    image_info->height = image_dim;
    image_info->stride = image_dim;

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         scenic::MemoryType::HOST_MEMORY, 0);

    EXPECT_EQ("ImagePipe::AddImage: Image can not be assigned an ID of 0.",
              reported_errors_.back());
  }
}

// Present two frames on the ImagePipe, making sure that acquire fence is
// being listened to and release fences are signalled.
TEST_F(ImagePipeTest, ImagePipePresentTwoFrames) {
  ImagePipePtr image_pipe =
      fxl::MakeRefCounted<ImagePipeThatCreatesDummyImages>(session_.get(),
                                                           this);

  uint32_t imageId1 = 1;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);

    auto image_info = scenic::ImageInfo::New();
    image_info->pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
    image_info->tiling = scenic::ImageInfo::Tiling::LINEAR;
    image_info->width = image_dim;
    image_info->height = image_dim;
    image_info->stride = image_dim;

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         scenic::MemoryType::HOST_MEMORY, 0);
  }

  // Make checkerboard the currently displayed image.
  mx::event acquire_fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &acquire_fence1));
  mx::event release_fence1;
  ASSERT_EQ(MX_OK, mx::event::create(0, &release_fence1));

  image_pipe->PresentImage(imageId1, 0, CopyEvent(acquire_fence1),
                           CopyEvent(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled
  // acquire fence yet.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, kFenceSignalled);

  // Run until image1 is presented.
  for (int i = 0; !image_pipe->GetEscherImage() && i < 400; i++) {
    image_pipe->Update(0u, 0u);
    ::mozart::test::RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(10));
  }

  ASSERT_TRUE(image_pipe->GetEscherImage());
  escher::ImagePtr image1 = image_pipe->GetEscherImage();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  uint32_t imageId2 = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = scenic::ImageInfo::New();
    image_info->pixel_format = scenic::ImageInfo::PixelFormat::BGRA_8;
    image_info->tiling = scenic::ImageInfo::Tiling::LINEAR;
    image_info->width = image_dim;
    image_info->height = image_dim;
    image_info->stride = image_dim;

    // Add the image to the image pipe.
    image_pipe->AddImage(imageId2, std::move(image_info),
                         CopyVmo(gradient->vmo()),
                         scenic::MemoryType::HOST_MEMORY, 0);
  }

  // The first image should not have been released.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(IsEventSignalled(release_fence1, kFenceSignalled));

  // Make gradient the currently displayed image.
  mx::event acquire_fence2;
  ASSERT_EQ(MX_OK, mx::event::create(0, &acquire_fence2));
  mx::event release_fence2;
  ASSERT_EQ(MX_OK, mx::event::create(0, &release_fence2));

  image_pipe->PresentImage(imageId2, 0, CopyEvent(acquire_fence2),
                           CopyEvent(release_fence2), nullptr);

  // Verify that the currently display image hasn't changed yet, since we
  // haven't signalled the acquire fence.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_EQ(image_pipe->GetEscherImage(), image1);

  // Signal on the acquire fence.
  acquire_fence2.signal(0u, kFenceSignalled);

  // There should be a new image presented.
  RUN_MESSAGE_LOOP_UNTIL(image1 != image_pipe->GetEscherImage());
  escher::ImagePtr image2 = image_pipe->GetEscherImage();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);

  // The first image should have been released.
  ASSERT_EQ(mock_release_fence_signaller_->num_calls_to_add_cpu_release_fence(),
            1u);
  ASSERT_TRUE(IsEventSignalled(release_fence1, kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(release_fence2, kFenceSignalled));
}

// TODO(MZ-151): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace test
}  // namespace scene_manager
