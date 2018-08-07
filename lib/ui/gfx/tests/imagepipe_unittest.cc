// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/host_image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/tests/mocks.h"
#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/util.h"
#include "gtest/gtest.h"
#include "lib/escher/flib/fence.h"
#include "lib/escher/util/image_utils.h"
#include "lib/ui/scenic/cpp/commands.h"

namespace scenic {
namespace gfx {
namespace test {

class ImagePipeTest : public SessionTest, public escher::ResourceManager {
 public:
  ImagePipeTest()
      : escher::ResourceManager(escher::EscherWeakPtr()),
        command_buffer_sequencer_() {}

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

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithBuffer(
    size_t buffer_size, std::unique_ptr<uint8_t[]> buffer_pixels) {
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

fuchsia::images::ImageInfo CreateImageInfoForBgra8Image(size_t w, size_t h) {
  fuchsia::images::ImageInfo image_info;
  image_info.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  image_info.tiling = fuchsia::images::Tiling::LINEAR;
  image_info.width = w;
  image_info.height = h;
  image_info.stride = w;
  return image_info;
}

fxl::RefPtr<fsl::SharedVmo> CreateVmoWithGradientPixels(size_t w, size_t h) {
  size_t pixels_size;
  auto pixels = escher::image_utils::NewGradientPixels(w, h, &pixels_size);
  return CreateVmoWithBuffer(pixels_size, std::move(pixels));
}

class ImagePipeThatCreatesDummyImages : public ImagePipe {
 public:
  ImagePipeThatCreatesDummyImages(
      Session* session, escher::ResourceManager* dummy_resource_manager)
      : ImagePipe(session, 0u),
        dummy_resource_manager_(dummy_resource_manager) {}

 private:
  // Override to create an Image without a backing escher::Image.
  ImagePtr CreateImage(Session* session, MemoryPtr memory,
                       const fuchsia::images::ImageInfo& image_info,
                       uint64_t memory_offset,
                       ErrorReporter* error_reporter) override {
    return HostImage::NewForTesting(session, 0u, dummy_resource_manager_,
                                    memory->As<HostMemory>());
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
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         fuchsia::images::MemoryType::HOST_MEMORY, 0);

    ExpectLastReportedError(
        "ImagePipe::AddImage: Image can not be assigned an ID of 0.");
  }
}

// Call Present with out-of-order presentation times, and expect an error.
TEST_F(ImagePipeTest, PresentImagesOutOfOrder) {
  ImagePipePtr image_pipe =
      fxl::MakeRefCounted<ImagePipeThatCreatesDummyImages>(session_.get(),
                                                           this);

  uint32_t imageId1 = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         fuchsia::images::MemoryType::HOST_MEMORY, 0);
  }
  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};

  image_pipe->PresentImage(imageId1, 1, CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()),
                           std::move(callback));
  image_pipe->PresentImage(imageId1, 0, CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()),
                           std::move(callback));

  ExpectLastReportedError(
      "scenic::gfx::ImagePipe: Present called with out-of-order presentation "
      "time.presentation_time=0, last scheduled presentation time=1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipeTest, PresentImagesInOrder) {
  ImagePipePtr image_pipe =
      fxl::MakeRefCounted<ImagePipeThatCreatesDummyImages>(session_.get(),
                                                           this);

  uint32_t imageId1 = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         fuchsia::images::MemoryType::HOST_MEMORY, 0);
  }
  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};

  image_pipe->PresentImage(imageId1, 1, CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()),
                           std::move(callback));
  image_pipe->PresentImage(imageId1, 1, CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()),
                           std::move(callback));

  EXPECT_ERROR_COUNT(0);
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
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(imageId1, std::move(image_info),
                         CopyVmo(checkerboard->vmo()),
                         fuchsia::images::MemoryType::HOST_MEMORY, 0);
  }

  // Make checkerboard the currently displayed image.
  zx::event acquire_fence1 = CreateEvent();
  zx::event release_fence1 = CreateEvent();

  image_pipe->PresentImage(imageId1, 0, CopyEventIntoFidlArray(acquire_fence1),
                           CopyEventIntoFidlArray(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled
  // acquire fence yet.
  RunLoopUntilIdle();
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, escher::kFenceSignalled);

  // Run until image1 is presented.
  RunLoopUntilIdle();
  ASSERT_TRUE(image_pipe->GetEscherImage());
  escher::ImagePtr image1 = image_pipe->GetEscherImage();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  uint32_t imageId2 = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe.
    image_pipe->AddImage(imageId2, std::move(image_info),
                         CopyVmo(gradient->vmo()),
                         fuchsia::images::MemoryType::HOST_MEMORY, 0);
  }

  // The first image should not have been released.
  RunLoopUntilIdle();
  ASSERT_FALSE(IsEventSignalled(release_fence1, escher::kFenceSignalled));

  // Make gradient the currently displayed image.
  zx::event acquire_fence2 = CreateEvent();
  zx::event release_fence2 = CreateEvent();

  image_pipe->PresentImage(imageId2, 0, CopyEventIntoFidlArray(acquire_fence2),
                           CopyEventIntoFidlArray(release_fence2), nullptr);

  // Verify that the currently display image hasn't changed yet, since we
  // haven't signalled the acquire fence.
  RunLoopUntilIdle();
  ASSERT_EQ(image_pipe->GetEscherImage(), image1);

  // Signal on the acquire fence.
  acquire_fence2.signal(0u, escher::kFenceSignalled);

  // There should be a new image presented.
  RunLoopUntilIdle();
  escher::ImagePtr image2 = image_pipe->GetEscherImage();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);

  // The first image should have been released.
  ASSERT_EQ(mock_release_fence_signaller_->num_calls_to_add_cpu_release_fence(),
            1u);
  ASSERT_TRUE(IsEventSignalled(release_fence1, escher::kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(release_fence2, escher::kFenceSignalled));
}

// TODO(MZ-151): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace test
}  // namespace gfx
}  // namespace scenic
