// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe.h"

#include <gtest/gtest.h>

#include "lib/ui/scenic/cpp/commands.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/image_pipe_unittest_common.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class ImagePipeThatCreatesFakeImages : public ImagePipe {
 public:
  ImagePipeThatCreatesFakeImages(gfx::Session* session,
                                 std::unique_ptr<ImagePipeUpdater> image_pipe_updater,
                                 escher::ResourceManager* fake_resource_manager)
      : ImagePipe(session, 0u, std::move(image_pipe_updater), session->shared_error_reporter()),
        fake_resource_manager_(fake_resource_manager) {}

  ImagePipeUpdateResults Update(scheduling::PresentId present_id) override {
    auto result = ImagePipe::Update(present_id);
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
    escher::ImagePtr escher_image = escher::Image::WrapVkImage(
        fake_resource_manager_, escher_info, vk::Image(), vk::ImageLayout::eUndefined);
    FX_CHECK(escher_image);
    auto image = fxl::AdoptRef(new FakeImage(session, id, escher_image));

    fake_images_.push_back(image);
    return image;
  }

  escher::ResourceManager* fake_resource_manager_;
};

// Creates test environment.
class ImagePipeTest : public ErrorReportingTest, public escher::ResourceManager {
 public:
  ImagePipeTest() : escher::ResourceManager(escher::EscherWeakPtr()) {}

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}

  void SetUp() override {
    ErrorReportingTest::SetUp();

    gfx_session_ = std::make_unique<gfx::Session>(/*id=*/1, SessionContext{},
                                                  shared_event_reporter(), shared_error_reporter());
    auto updater = std::make_unique<MockImagePipeUpdater>();
    image_pipe_updater_ = updater.get();
    image_pipe_ = fxl::MakeRefCounted<ImagePipeThatCreatesFakeImages>(gfx_session_.get(),
                                                                      std::move(updater), this);
  }

  void TearDown() override {
    image_pipe_.reset();
    image_pipe_updater_ = nullptr;
    gfx_session_.reset();

    ErrorReportingTest::TearDown();
  }

  fxl::RefPtr<ImagePipeThatCreatesFakeImages> image_pipe_;
  MockImagePipeUpdater* image_pipe_updater_;

 private:
  std::unique_ptr<gfx::Session> gfx_session_;
};

// Present an image with an Id of zero, and expect an error.
TEST_F(ImagePipeTest, ImagePipeImageIdMustNotBeZero) {
  uint32_t image1_id = 0;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);

    ExpectLastReportedError("ImagePipe::AddImage: Image can not be assigned an ID of 0.");
  }
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipeTest, PresentImage_ShouldCallScheduleUpdate) {
  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  EXPECT_EQ(image_pipe_updater_->schedule_update_call_count_, 0u);

  auto present_id =
      image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                                CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_EQ(image_pipe_updater_->schedule_update_call_count_, 1u);

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Call Present with out-of-order presentation times, and expect an error.
TEST_F(ImagePipeTest, PresentImagesOutOfOrder) {
  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(image1_id, zx::time(0), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  ExpectLastReportedError(
      "ImagePipe: Present called with out-of-order presentation "
      "time. presentation_time=0, last scheduled presentation time=1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipeTest, PresentImagesInOrder) {
  uint32_t image1_id = 1;
  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Call Present with an image with an offset into its memory, and expect no
// error.
TEST_F(ImagePipeTest, PresentImagesWithOffset) {
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
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(shared_vmo->vmo()),
                          offset_bytes, GetVmoSize(shared_vmo->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(image1_id, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Present two frames on the ImagePipe, making sure that acquire fence is
// being listened to and release fences are signalled.
TEST_F(ImagePipeTest, ImagePipePresentTwoFrames) {
  uint32_t image1_id = 1;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  const auto present_id1 =
      image_pipe_->PresentImage(image1_id, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  image_pipe_->Update(present_id1);
  ASSERT_TRUE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  // Image should now be presented.
  ImagePtr image1 = image_pipe_->current_image();
  ASSERT_TRUE(image1);
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  uint32_t image2_id = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe.
    image_pipe_->AddImage(image2_id, std::move(image_info), CopyVmo(gradient->vmo()), 0,
                          GetVmoSize(gradient->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  const auto present_id2 =
      image_pipe_->PresentImage(image2_id, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Verify that the currently display image hasn't changed yet, since we haven't updated the image
  // pipe.
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_EQ(image_pipe_->current_image(), image1);

  image_pipe_->Update(present_id2);

  // There should be a new image presented.
  ImagePtr image2 = image_pipe_->current_image();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);
  ASSERT_FALSE(image_pipe_->GetEscherImage());
}

// Present two frames on the ImagePipe, but only update the second. Make sure ImagePipe updates to
// the second image correctly.
TEST_F(ImagePipeTest, ImagePipeUpdateTwoFrames) {
  // Image A is a 2x2 image with id=2.
  // Image B is a 4x4 image with id=4.
  uint32_t imageIdA = 2;
  uint32_t imageIdB = 4;
  auto image_info_a = CreateImageInfoForBgra8Image(imageIdA, imageIdA);
  auto image_info_b = CreateImageInfoForBgra8Image(imageIdB, imageIdB);
  auto gradient_a = CreateVmoWithGradientPixels(imageIdA, imageIdA);
  auto gradient_b = CreateVmoWithGradientPixels(imageIdB, imageIdB);
  image_pipe_->AddImage(imageIdA, std::move(image_info_a), CopyVmo(gradient_a->vmo()), 0,
                        GetVmoSize(gradient_a->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  image_pipe_->AddImage(imageIdB, std::move(image_info_b), CopyVmo(gradient_b->vmo()), 0,
                        GetVmoSize(gradient_b->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);

  image_pipe_->PresentImage(imageIdA, zx::time(0), std::vector<zx::event>(),
                            std::vector<zx::event>(), /*callback=*/[](auto) {});
  const auto present_id =
      image_pipe_->PresentImage(imageIdB, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});

  image_pipe_->Update(present_id);

  auto image_out = image_pipe_->current_image();
  // We should get the second image in the queue, since both should have been
  // ready.
  ASSERT_TRUE(image_out);
  ASSERT_EQ(static_cast<FakeImage*>(image_out.get())->image_info_.width, imageIdB);
  ASSERT_EQ(image_pipe_->fake_images_.size(), 2u);
  ASSERT_EQ(image_pipe_->fake_images_[0]->update_count_, 0u);
  ASSERT_EQ(image_pipe_->fake_images_[1]->update_count_, 1u);

  // Do it again, to make sure that update is called a second time (since
  // released images could be edited by the client before presentation).
  const auto present_id2 =
      image_pipe_->PresentImage(imageIdA, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});
  const auto present_id3 =
      image_pipe_->PresentImage(imageIdB, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});

  image_pipe_->Update(present_id2);
  image_pipe_->Update(present_id3);

  image_out = image_pipe_->current_image();
  ASSERT_EQ(image_pipe_->fake_images_.size(), 2u);
  // Because Present was handled for image A, we should have a call to
  // UpdatePixels for that image.
  ASSERT_EQ(image_pipe_->fake_images_[0]->update_count_, 1u);
  ASSERT_EQ(image_pipe_->fake_images_[1]->update_count_, 2u);
}

// Present two frames on the ImagePipe. After presenting the first image but
// before signaling its acquire fence, remove it. Verify that this doesn't
// cause any errors.
TEST_F(ImagePipeTest, ImagePipeRemoveImageThatIsPendingPresent) {
  uint32_t image1_id = 1;

  // Create a checkerboard image and copy it into a vmo.
  {
    size_t image_dim = 100;
    auto checkerboard = CreateVmoWithCheckerboardPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe with ImagePipe.AddImage().
    image_pipe_->AddImage(image1_id, std::move(image_info), CopyVmo(checkerboard->vmo()), 0,
                          GetVmoSize(checkerboard->vmo()),
                          fuchsia::images::MemoryType::HOST_MEMORY);
  }

  const auto present_id1 =
      image_pipe_->PresentImage(image1_id, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Current presented image should be null, since we haven't called Update yet.
  ASSERT_FALSE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  // Remove the image; by the ImagePipe semantics, the consumer will
  // still keep a reference to it so any future presents will still work.
  image_pipe_->RemoveImage(image1_id);

  // Update the image.
  image_pipe_->Update(present_id1);
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ImagePtr image1 = image_pipe_->current_image();

  // Current image should now be updated.
  ASSERT_TRUE(image1);

  uint32_t image2_id = 2;
  // Create a new Image with a gradient.
  {
    size_t image_dim = 100;
    auto gradient = CreateVmoWithGradientPixels(image_dim, image_dim);
    auto image_info = CreateImageInfoForBgra8Image(image_dim, image_dim);

    // Add the image to the image pipe.
    image_pipe_->AddImage(image2_id, std::move(image_info), CopyVmo(gradient->vmo()), 0,
                          GetVmoSize(gradient->vmo()), fuchsia::images::MemoryType::HOST_MEMORY);
  }

  // Make gradient the currently displayed image.
  const auto present_id2 =
      image_pipe_->PresentImage(image2_id, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Verify that the currently display image hasn't changed yet, since we
  // haven't called Update yet.
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_EQ(image_pipe_->current_image(), image1);

  image_pipe_->Update(present_id2);

  // There should be a new image current image.
  ImagePtr image2 = image_pipe_->current_image();
  ASSERT_TRUE(image2);
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_NE(image1, image2);

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// TODO(fxbug.dev/23406): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
