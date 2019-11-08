// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

#include "gtest/gtest.h"
#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/tests/image_pipe_unittest_common.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/gfx/tests/session_handler_test.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class ImagePipeTest : public SessionHandlerTest, public escher::ResourceManager {
 public:
  ImagePipeTest() : escher::ResourceManager(escher::EscherWeakPtr()) {}

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}
};

class ImagePipeThatCreatesFakeImages : public ImagePipe {
 public:
  ImagePipeThatCreatesFakeImages(Session* session, escher::ResourceManager* fake_resource_manager)
      : ImagePipe(session, 0u, session->image_pipe_updater(), session->shared_error_reporter()),
        fake_resource_manager_(fake_resource_manager) {
    FXL_CHECK(session->session_context().frame_scheduler);
  }

  ImagePipeUpdateResults Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                zx::time presentation_time) override {
    auto result = ImagePipe::Update(release_fence_signaller, presentation_time);
    if (result.image_updated) {
      // Since we do not have renderer visitors to trigger |Image::UpdatePixels()| in test,
      // we count the image update/upload counts in |ImagePipe::Update()| instead.
      static_cast<FakeImage*>(current_image().get())->update_count_++;
    }
    return result;
  }

  std::vector<fxl::RefPtr<FakeImage>> fake_images_;

 private:
  // Override to create an Image without a backing escher::Image.
  ImagePtr CreateImage(Session* session, ResourceId id, MemoryPtr memory,
                       const fuchsia::images::ImageInfo& image_info,
                       uint64_t memory_offset) override {
    escher::ImageInfo escher_info;
    escher_info.width = image_info.width;
    escher_info.height = image_info.height;
    escher::ImagePtr escher_image =
        escher::Image::WrapVkImage(fake_resource_manager_, escher_info, vk::Image());
    FXL_CHECK(escher_image);
    auto image = fxl::AdoptRef(new FakeImage(session, id, escher_image));

    fake_images_.push_back(image);
    return image;
  }

  escher::ResourceManager* fake_resource_manager_;
};

// Present an image with an Id of zero, and expect an error.
TEST_F(ImagePipeTest, ImagePipeImageIdMustNotBeZero) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);
  uint32_t image1_id = 0;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                         GetVmoSize(checkerboard->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);

    ExpectLastReportedError("ImagePipe::AddImage: Image can not be assigned an ID of 0.");
  }
}

// Call Present with out-of-order presentation times, and expect an error.
TEST_F(ImagePipeTest, PresentImagesOutOfOrder) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                         GetVmoSize(checkerboard->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }
  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};

  image_pipe->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(image1_id, zx::time(0), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  ExpectLastReportedError(
      "ImagePipe: Present called with out-of-order presentation "
      "time. presentation_time=0, last scheduled presentation time=1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipeTest, PresentImagesInOrder) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                         GetVmoSize(checkerboard->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }
  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};

  image_pipe->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  EXPECT_ERROR_COUNT(0);
}

// Call Present with an image with an offset into its memory, and expect no
// error.
TEST_F(ImagePipeTest, PresentImagesWithOffset) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t w = 100;
    size_t h = 100;
    size_t offset_bytes = 10;
    size_t pixels_size;
    auto pixels = escher::image_utils::NewCheckerboardPixels(w, h, &pixels_size);
    auto shared_vmo = CreateSharedVmo(pixels_size + offset_bytes);
    memcpy(shared_vmo->Map(), pixels.get() + offset_bytes, pixels_size - offset_bytes);

    auto image_info = CreateImageInfoForBgra8Image(w, h);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(shared_vmo->vmo()), offset_bytes,
                         GetVmoSize(shared_vmo->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }
  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};

  image_pipe->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  EXPECT_ERROR_COUNT(0);
}

// Present two frames on the ImagePipe, making sure that acquire fence is
// being listened to and release fences are signalled.
TEST_F(ImagePipeTest, ImagePipePresentTwoFrames) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  uint32_t image1_id = 1;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                         GetVmoSize(checkerboard->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  // Make checkerboard the currently displayed image.
  zx::event acquire_fence1 = CreateEvent();
  zx::event release_fence1 = CreateEvent();

  image_pipe->PresentImage(image1_id, zx::time(0), CopyEventIntoFidlArray(acquire_fence1),
                           CopyEventIntoFidlArray(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled
  // acquire fence yet.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, escher::kFenceSignalled);

  // Run until image1 is presented, but not rendered due to lack of engine renderer visitor.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ASSERT_TRUE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Image should now be presented.
  ImagePtr image1 = image_pipe->current_image();
  ASSERT_TRUE(image1);
  ASSERT_FALSE(image_pipe->GetEscherImage());

  uint32_t image2_id = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe.
    image_pipe->AddImage(image2_id, std::move(image_info), CopyVmo(gradient->vmo()), 0,
                         GetVmoSize(gradient->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  // The first image should not have been released.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(IsEventSignalled(release_fence1, escher::kFenceSignalled));

  // Make gradient the currently displayed image.
  zx::event acquire_fence2 = CreateEvent();
  zx::event release_fence2 = CreateEvent();

  image_pipe->PresentImage(image2_id, zx::time(0), CopyEventIntoFidlArray(acquire_fence2),
                           CopyEventIntoFidlArray(release_fence2), nullptr);

  // Verify that the currently display image hasn't changed yet, since we
  // haven't signalled the acquire fence.
  ASSERT_FALSE(RunLoopUntilIdle());
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ASSERT_EQ(image_pipe->current_image(), image1);

  // Signal on the acquire fence.
  acquire_fence2.signal(0u, escher::kFenceSignalled);

  // There should be a new image presented.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ImagePtr image2 = image_pipe->current_image();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // The first image should have been released.
  ASSERT_TRUE(IsEventSignalled(release_fence1, escher::kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(release_fence2, escher::kFenceSignalled));
}

// Present two frames on the ImagePipe, making sure that UpdatePixels is only
// called on images that are acquired and used.
TEST_F(ImagePipeTest, ImagePipeUpdateTwoFrames) {
  auto image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  // Image A is a 2x2 image with id=2.
  // Image B is a 4x4 image with id=4.
  uint32_t imageIdA = 2;
  uint32_t imageIdB = 4;
  auto image_info_a = CreateImageInfoForBgra8Image(imageIdA, imageIdA);
  auto image_info_b = CreateImageInfoForBgra8Image(imageIdB, imageIdB);
  auto gradient_a = CreateVmoWithGradientPixels(imageIdA, imageIdA);
  auto gradient_b = CreateVmoWithGradientPixels(imageIdB, imageIdB);
  image_pipe->AddImage(imageIdA, std::move(image_info_a), CopyVmo(gradient_a->vmo()), 0,
                       GetVmoSize(gradient_a->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  image_pipe->AddImage(imageIdB, std::move(image_info_b), CopyVmo(gradient_b->vmo()), 0,
                       GetVmoSize(gradient_b->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);

  image_pipe->PresentImage(imageIdA, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  image_pipe->PresentImage(imageIdB, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);

  // Let all updates get scheduled and finished
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));

  auto image_out = image_pipe->current_image();
  // We should get the second image in the queue, since both should have been
  // ready.
  ASSERT_TRUE(image_out);
  ASSERT_EQ(static_cast<FakeImage*>(image_out.get())->image_info_.width, imageIdB);
  ASSERT_EQ(image_pipe->fake_images_.size(), 2u);
  ASSERT_EQ(image_pipe->fake_images_[0]->update_count_, 0u);
  ASSERT_EQ(image_pipe->fake_images_[1]->update_count_, 1u);

  // Do it again, to make sure that update is called a second time (since
  // released images could be edited by the client before presentation).
  //
  // In this case, we need to run to idle after presenting image A, so that
  // image B is returned by the pool, marked dirty, and is free to be acquired
  // again.
  image_pipe->PresentImage(imageIdA, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  image_pipe->PresentImage(imageIdB, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));

  image_out = image_pipe->current_image();
  ASSERT_EQ(image_pipe->fake_images_.size(), 2u);
  // Because Present was handled for image A, we should have a call to
  // UpdatePixels for that image.
  ASSERT_EQ(image_pipe->fake_images_[0]->update_count_, 1u);
  ASSERT_EQ(image_pipe->fake_images_[1]->update_count_, 2u);
}

// Present two frames on the ImagePipe. After presenting the first image but
// before signaling its acquire fence, remove it. Verify that this doesn't
// cause any errors.
TEST_F(ImagePipeTest, ImagePipeRemoveImageThatIsPendingPresent) {
  ImagePipePtr image_pipe = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(session(), this);

  uint32_t image1_id = 1;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                         GetVmoSize(checkerboard->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  // Make checkerboard the currently displayed image.
  zx::event acquire_fence1 = CreateEvent();
  zx::event release_fence1 = CreateEvent();

  image_pipe->PresentImage(image1_id, zx::time(0), CopyEventIntoFidlArray(acquire_fence1),
                           CopyEventIntoFidlArray(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled
  // acquire fence yet.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Remove the image; by the ImagePipe semantics, the consumer will
  // still keep a reference to it so any future presents will still work.
  image_pipe->RemoveImage(image1_id);

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, escher::kFenceSignalled);

  // Run until image1 is presented, but image1 is not updated due to lack of engine renderer
  // visitor.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ImagePtr image1 = image_pipe->current_image();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  uint32_t image2_id = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe.
    image_pipe->AddImage(image2_id, std::move(image_info), CopyVmo(gradient->vmo()), 0,
                         GetVmoSize(gradient->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  // The first image should not have been released.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(IsEventSignalled(release_fence1, escher::kFenceSignalled));

  // Make gradient the currently displayed image.
  zx::event acquire_fence2 = CreateEvent();
  zx::event release_fence2 = CreateEvent();

  image_pipe->PresentImage(image2_id, zx::time(0), CopyEventIntoFidlArray(acquire_fence2),
                           CopyEventIntoFidlArray(release_fence2), nullptr);

  // Verify that the currently display image hasn't changed yet, since we
  // haven't signalled the acquire fence.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ASSERT_EQ(image_pipe->current_image(), image1);

  // Signal on the acquire fence.
  acquire_fence2.signal(0u, escher::kFenceSignalled);

  // There should be a new image presented.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ImagePtr image2 = image_pipe->current_image();
  ASSERT_TRUE(image2);
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ASSERT_NE(image1, image2);

  // The first image should have been released.
  ASSERT_TRUE(IsEventSignalled(release_fence1, escher::kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(release_fence2, escher::kFenceSignalled));
  EXPECT_ERROR_COUNT(0);
}

// TODO(SCN-151): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
