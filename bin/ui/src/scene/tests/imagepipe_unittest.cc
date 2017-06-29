// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/image_utils.h"
#include "gtest/gtest.h"

#include "apps/mozart/lib/scene/session_helpers.h"
#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/src/scene/acquire_fence.h"
#include "apps/mozart/src/scene/fence.h"
#include "apps/mozart/src/scene/resources/image_pipe.h"
#include "apps/mozart/src/scene/tests/session_test.h"
#include "apps/mozart/src/scene/tests/util.h"

namespace mozart {
namespace scene {
namespace test {

class ReleaseFenceSignallerForTest : public ReleaseFenceSignaller {
 public:
  ReleaseFenceSignallerForTest(
      escher::impl::CommandBufferSequencer* command_buffer_sequencer)
      : ReleaseFenceSignaller(command_buffer_sequencer){};

  void AddCPUReleaseFence(mx::event fence) override {
    num_calls_to_add_cpu_release_fence_++;
    // Signal immediately for testing purposes.
    fence.signal(0u, kFenceSignalled);
  };

  uint32_t num_calls_to_add_cpu_release_fence() {
    return num_calls_to_add_cpu_release_fence_;
  }

 private:
  uint32_t num_calls_to_add_cpu_release_fence_ = 0;
};

class ImagePipeTest : public SessionTest, public escher::ResourceManager {
 public:
  ImagePipeTest()
      : escher::ResourceManager(escher::VulkanContext()),
        command_buffer_sequencer_() {}

  std::unique_ptr<SessionContext> CreateSessionContext() override {
    auto r = std::make_unique<ReleaseFenceSignallerForTest>(
        &command_buffer_sequencer_);
    mock_release_fence_signaller_ = r.get();
    return std::make_unique<SessionContextForTest>(std::move(r));
  }

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}

  escher::impl::CommandBufferSequencer command_buffer_sequencer_;
  ReleaseFenceSignallerForTest* mock_release_fence_signaller_;
};

TEST_F(ImagePipeTest, SimpleAcquireFenceSignalling) {
  // Create an AcquireFence.
  mx::event fence1;
  ASSERT_EQ(mx::event::create(0, &fence1), MX_OK);
  AcquireFence buffer_fence1(CopyEvent(fence1));

  // Expect that it is not signalled initially.
  EXPECT_FALSE(buffer_fence1.WaitReady(ftl::TimeDelta::Zero()));

  // Signal the fence.
  fence1.signal(0u, kFenceSignalled);

  // Expect that it is signalled now.
  EXPECT_TRUE(buffer_fence1.WaitReady(ftl::TimeDelta::Zero()));

  // TODO: Test WaitAsync and callbacks.
}

ftl::RefPtr<mtl::SharedVmo> CreateVmoWithBuffer(
    size_t buffer_size,
    std::unique_ptr<uint8_t[]> buffer_pixels) {
  auto shared_vmo = CreateSharedVmo(buffer_size);

  memcpy(shared_vmo->Map(), buffer_pixels.get(), buffer_size);
  return shared_vmo;
}

ftl::RefPtr<mtl::SharedVmo> CreateVmoWithCheckerboardPixels(size_t w,
                                                            size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewCheckerboardPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

ftl::RefPtr<mtl::SharedVmo> CreateVmoWithGradientPixels(size_t w, size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewGradientPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

class ImagePipeThatCreatesDummyImages : public ImagePipe {
 public:
  ImagePipeThatCreatesDummyImages(
      Session* session,
      escher::ResourceManager* dummy_resource_manager)
      : ImagePipe(session), dummy_resource_manager_(dummy_resource_manager) {}

 private:
  // Override to create an Image without a backing escher::Image.
  ImagePtr CreateImage(Session* session,
                       MemoryPtr memory,
                       const mozart2::ImageInfoPtr& image_info,
                       uint64_t memory_offset,
                       ErrorReporter* error_reporter) override {
    return Image::NewForTesting(session, dummy_resource_manager_, memory);
  }
  escher::ResourceManager* dummy_resource_manager_;
};

// How long to run the message loop when we want to allow a task in the
// task queue to run.
constexpr ftl::TimeDelta kPumpMessageLoopDuration =
    ftl::TimeDelta::FromMilliseconds(100);

// Present two frames on the ImagePipe, making sure that acquire fence is being
// listened to and release fences are signalled.
TEST_F(ImagePipeTest, ImagePipePresentTwoFrames) {
  ImagePipePtr image_pipe =
      ftl::MakeRefCounted<ImagePipeThatCreatesDummyImages>(session_.get(),
                                                           this);

  uint32_t imageId1 = 0;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);

    auto image_info = mozart2::ImageInfo::New();
    image_info->pixel_format = mozart2::ImageInfo::PixelFormat::BGRA_8;
    image_info->tiling = mozart2::ImageInfo::Tiling::LINEAR;
    image_info->width = image_dim;
    image_info->height = image_dim;
    image_info->stride = image_dim;

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         mozart2::MemoryType::HOST_MEMORY, 0);
  }

  // Make checkerboard the currently displayed image.
  mx::event acquire_fence1;
  ASSERT_EQ(mx::event::create(0, &acquire_fence1), MX_OK);
  mx::event release_fence1;
  ASSERT_EQ(mx::event::create(0, &release_fence1), MX_OK);

  image_pipe->PresentImage(imageId1, CopyEvent(acquire_fence1),
                           CopyEvent(release_fence1));

  // Current presented image should be null, since we haven't signalled
  // acquire fence yet.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, kFenceSignalled);

  // Run until image1 is presented.
  RUN_MESSAGE_LOOP_UNTIL(image_pipe->GetEscherImage());
  escher::ImagePtr image1 = image_pipe->GetEscherImage();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  uint32_t imageId2 = 1;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = mozart2::ImageInfo::New();
    image_info->pixel_format = mozart2::ImageInfo::PixelFormat::BGRA_8;
    image_info->tiling = mozart2::ImageInfo::Tiling::LINEAR;
    image_info->width = image_dim;
    image_info->height = image_dim;
    image_info->stride = image_dim;

    // Add the image to the image pipe.
    image_pipe->AddImage(imageId2, std::move(image_info),
                         CopyVmo(gradient->vmo()),
                         mozart2::MemoryType::HOST_MEMORY, 0);
  }

  // The first image should not have been released.
  ::mozart::test::RunLoopWithTimeout(kPumpMessageLoopDuration);
  ASSERT_FALSE(IsEventSignalled(release_fence1, kFenceSignalled));

  // Make gradient the currently displayed image.
  mx::event acquire_fence2;
  ASSERT_EQ(mx::event::create(0, &acquire_fence2), MX_OK);
  mx::event release_fence2;
  ASSERT_EQ(mx::event::create(0, &release_fence2), MX_OK);

  image_pipe->PresentImage(imageId2, CopyEvent(acquire_fence2),
                           CopyEvent(release_fence2));

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
}  // namespace scene
}  // namespace mozart
