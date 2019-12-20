// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe2.h"

#include <lib/fdio/directory.h>
#include <lib/ui/scenic/cpp/commands.h>

#include "gtest/gtest.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/tests/image_pipe_unittest_common.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
#include "src/ui/scenic/lib/gfx/tests/session_handler_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"

namespace scenic_impl::gfx::test {

namespace {

struct SysmemTokens {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
};

SysmemTokens CreateSysmemTokens(fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                                bool duplicate_token) {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  if (duplicate_token) {
    status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
  }

  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);

  return {std::move(local_token), std::move(dup_token)};
}

void SetConstraints(fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t width,
                    uint32_t height, uint32_t image_count,
                    fuchsia::sysmem::PixelFormatType pixel_format, bool wait_for_buffers_allocated,
                    fuchsia::sysmem::BufferCollectionSyncPtr* buffer_collection_output) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  zx_status_t status =
      sysmem_allocator->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.min_buffer_count = image_count;
  constraints.usage.vulkan = fuchsia::sysmem::vulkanUsageTransferSrc;
  if (width && height) {
    constraints.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints =
        constraints.image_format_constraints[0];
    image_constraints = fuchsia::sysmem::ImageFormatConstraints();
    image_constraints.required_min_coded_width = width;
    image_constraints.required_min_coded_height = height;
    image_constraints.required_max_coded_width = width;
    image_constraints.required_max_coded_height = height;
    image_constraints.max_coded_width = width * 4;
    image_constraints.max_coded_height = width * 4;
    image_constraints.max_bytes_per_row = 0xffffffff;
    image_constraints.pixel_format.type = pixel_format;
    image_constraints.color_spaces_count = 1;
    switch (pixel_format) {
      case fuchsia::sysmem::PixelFormatType::BGRA32:
        image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
        break;
      case fuchsia::sysmem::PixelFormatType::NV12:
        image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
        break;
      default:
        FXL_NOTREACHED();
    }
  }

  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);

  if (wait_for_buffers_allocated) {
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    EXPECT_GE(buffer_collection_info.buffer_count, image_count);
  }

  if (buffer_collection_output) {
    *buffer_collection_output = std::move(buffer_collection);
  } else {
    status = buffer_collection->Close();
    EXPECT_EQ(status, ZX_OK);
  }
}

}  // namespace

class CreateImagePipe2CmdTest : public VkSessionTest {};

VK_TEST_F(CreateImagePipe2CmdTest, ApplyCommand) {
  zx::channel image_pipe_endpoint;
  zx::channel remote_endpoint;
  zx::channel::create(0, &image_pipe_endpoint, &remote_endpoint);

  const uint32_t kImagePipeId = 1;
  ASSERT_TRUE(Apply(scenic::NewCreateImagePipe2Cmd(
      kImagePipeId,
      fidl::InterfaceRequest<fuchsia::images::ImagePipe2>(std::move(remote_endpoint)))));
}

class ImagePipe2ThatCreatesFakeImages : public ImagePipe2 {
 public:
  ImagePipe2ThatCreatesFakeImages(Session* session,
                                  fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request,
                                  escher::ResourceManager* fake_resource_manager)
      : ImagePipe2(session, 0u, std::move(request), CreateImagePipeUpdater(session),
                   session->shared_error_reporter()),
        fake_resource_manager_(fake_resource_manager) {
    FXL_CHECK(session->session_context().frame_scheduler);

    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);
  }
  ~ImagePipe2ThatCreatesFakeImages() { CloseConnectionAndCleanUp(); }

  ImagePipeUpdateResults Update(escher::ReleaseFenceSignaller* release_fence_signaller,
                                zx::time presentation_time) override {
    auto result = ImagePipe2::Update(release_fence_signaller, presentation_time);
    if (result.image_updated) {
      static_cast<FakeImage*>(current_image().get())->update_count_++;
    }
    return result;
  }

  fuchsia::sysmem::Allocator_Sync* sysmem_allocator() { return sysmem_allocator_.get(); }

  void set_next_image_is_protected(bool is_protected) { next_image_is_protected_ = is_protected; }

  fuchsia::sysmem::PixelFormatType pixel_format_;
  std::vector<fxl::RefPtr<FakeImage>> fake_images_;

 private:
  bool SetBufferCollectionConstraints(
      Session* session, fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
      const vk::ImageCreateInfo& create_info,
      vk::BufferCollectionFUCHSIA* out_buffer_collection_fuchsia) override {
    SetConstraints(sysmem_allocator_.get(), std::move(token), 0u, 0u, 1u,
                   fuchsia::sysmem::PixelFormatType::BGRA32, false, nullptr);
    return true;
  }

  void DestroyBufferCollection(Session* session,
                               const vk::BufferCollectionFUCHSIA& vk_buffer_collection) override {}

  ImagePtr CreateImage(Session* session, ResourceId image_id,
                       const ImagePipe2::BufferCollectionInfo& info,
                       uint32_t buffer_collection_index,
                       const ::fuchsia::sysmem::ImageFormat_2& image_format) override {
    pixel_format_ = info.buffer_collection_info.settings.image_format_constraints.pixel_format.type;
    escher::ImageInfo escher_info;
    escher_info.width = image_format.coded_width;
    escher_info.height = image_format.coded_height;
    if (next_image_is_protected_) {
      escher_info.memory_flags |= vk::MemoryPropertyFlagBits::eProtected;
      next_image_is_protected_ = false;
    }
    escher::ImagePtr escher_image = escher::Image::WrapVkImage(
        fake_resource_manager_, escher_info, vk::Image(), vk::ImageLayout::eUndefined);
    FXL_CHECK(escher_image);
    auto image = fxl::AdoptRef(new FakeImage(session, image_id, escher_image));
    fake_images_.push_back(image);
    return image;
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  escher::ResourceManager* fake_resource_manager_;
  bool next_image_is_protected_ = false;
};

// Creates test environment.
class ImagePipe2Test : public SessionHandlerTest, public escher::ResourceManager {
 public:
  ImagePipe2Test() : escher::ResourceManager(escher::EscherWeakPtr()) {}

  fxl::RefPtr<ImagePipe2ThatCreatesFakeImages> CreateImagePipe() {
    return fxl::MakeRefCounted<ImagePipe2ThatCreatesFakeImages>(
        session(), image_pipe_handle_.NewRequest(), this);
  }

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}

 private:
  fidl::InterfacePtr<fuchsia::images::ImagePipe2> image_pipe_handle_;
};

TEST_F(ImagePipe2Test, CreateAndDestroyImagePipe) { auto image_pipe = CreateImagePipe(); }

// Present a BufferCollection with an Id of zero, and expect an error.
TEST_F(ImagePipe2Test, BufferCollectionIdMustNotBeZero) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), false);

  const uint32_t kBufferId = 0;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  ExpectLastReportedError("AddBufferCollection: BufferCollection can not be assigned an ID of 0.");
}

// Present an image with an Id of zero, and expect an error.
TEST_F(ImagePipe2Test, ImagePipeImageIdMustNotBeZero) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), false);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kImageId = 0;
  image_pipe->AddImage(kImageId, kBufferId, 0, fuchsia::sysmem::ImageFormat_2());

  ExpectLastReportedError("AddImage: Image can not be assigned an ID of 0.");
}

// Add multiple images from same buffer collection.
TEST_F(ImagePipe2Test, AddMultipleImagesFromABufferCollection) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId, 1, image_format);

  EXPECT_ERROR_COUNT(0);
}

// Add multiple images from an invalid buffer collection id.
TEST_F(ImagePipe2Test, BufferCollectionIdMustBeValid) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId + 1, 1, image_format);

  ExpectLastReportedError("AddImage: resource with ID not found.");
}

// Add multiple images from same buffer collection.
TEST_F(ImagePipe2Test, BufferCollectionIndexMustBeValid) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId, kImageCount, image_format);

  ExpectLastReportedError("AddImage: buffer_collection_index out of bounds");
}

// Removing buffer collection removes associated images.
TEST_F(ImagePipe2Test, RemoveBufferCollectionRemovesImages) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  image_pipe->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);

  // Remove buffer collection
  image_pipe->RemoveBufferCollection(kBufferId);
  image_pipe->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);

  ExpectLastReportedError("PresentImage: could not find Image with ID: 1");
}

// Call Present with out-of-order presentation times, and expect an error.
TEST_F(ImagePipe2Test, PresentImagesOutOfOrder) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);

  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};
  image_pipe->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(kImageId, zx::time(0), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  ExpectLastReportedError(
      "PresentImage: Present called with out-of-order presentation "
      "time. presentation_time=0, last scheduled presentation time=1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipe2Test, PresentImagesInOrder) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);

  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};
  image_pipe->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  EXPECT_ERROR_COUNT(0);
}

// Call Present with an image with an odd size(possible offset) into its memory, and expect no
// error.
TEST_F(ImagePipe2Test, PresentImagesWithOddSize) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 35;
  const uint32_t kHeight = 35;
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, &buffer_collection);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe->AddImage(kImageId, kBufferId, 0, image_format);

  fuchsia::images::ImagePipe::PresentImageCallback callback = [](auto) {};
  image_pipe->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));
  image_pipe->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                           CopyEventIntoFidlArray(CreateEvent()), std::move(callback));

  EXPECT_ERROR_COUNT(0);
}

// Present two frames on the ImagePipe, making sure that both buffers are allocated, acquire fence
// is being listened to and release fences are signalled.
TEST_F(ImagePipe2Test, ImagePipePresentTwoFrames) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  zx::event acquire_fence1 = CreateEvent();
  zx::event release_fence1 = CreateEvent();
  image_pipe->PresentImage(kImageId1, zx::time(0), CopyEventIntoFidlArray(acquire_fence1),
                           CopyEventIntoFidlArray(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled acquire fence yet.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, escher::kFenceSignalled);

  // Run until image1 is presented, but image1 will not be rendered since we have no engine render
  // visitor.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ASSERT_TRUE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Image should now be presented.
  ImagePtr image1 = image_pipe->current_image();
  ASSERT_TRUE(image1);

  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId, 1, image_format);

  // The first image should not have been released.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(IsEventSignalled(release_fence1, escher::kFenceSignalled));

  // Make gradient the currently displayed image.
  zx::event acquire_fence2 = CreateEvent();
  zx::event release_fence2 = CreateEvent();

  image_pipe->PresentImage(kImageId2, zx::time(0), CopyEventIntoFidlArray(acquire_fence2),
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
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ImagePtr image2 = image_pipe->current_image();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);

  // The first image should have been released.
  ASSERT_TRUE(IsEventSignalled(release_fence1, escher::kFenceSignalled));
  ASSERT_FALSE(IsEventSignalled(release_fence2, escher::kFenceSignalled));
}

// Present two frames on the ImagePipe, making sure that UpdatePixels is only called on images that
// are acquired and used.
TEST_F(ImagePipe2Test, ImagePipeUpdateTwoFrames) {
  auto image_pipe = CreateImagePipe();

  // Add first image 32x32
  auto tokens1 = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBuffer1Id = 1;
  image_pipe->AddBufferCollection(kBuffer1Id, std::move(tokens1.local_token));

  const uint32_t kImage1Width = 32;
  const uint32_t kImage1Height = 32;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens1.dup_token), kImage1Width,
                 kImage1Height, 1u, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format1 = {};
  image_format1.coded_width = kImage1Width;
  image_format1.coded_height = kImage1Height;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBuffer1Id, 0, image_format1);

  // Add second image 48x48
  auto tokens2 = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBuffer2Id = 2;
  image_pipe->AddBufferCollection(kBuffer2Id, std::move(tokens2.local_token));

  const uint32_t kImage2Width = 48;
  const uint32_t kImage2Height = 48;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens2.dup_token), kImage2Width,
                 kImage2Height, 1u, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format_2 = {};
  image_format_2.coded_width = kImage2Width;
  image_format_2.coded_height = kImage2Height;
  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBuffer2Id, 0, image_format_2);

  // Present both images
  image_pipe->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  image_pipe->PresentImage(kImageId2, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);

  // Let all updates get scheduled and finished
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));

  auto image_out = image_pipe->current_image();
  // We should get the second image in the queue, since both should have been ready.
  ASSERT_TRUE(image_out);
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ASSERT_EQ(static_cast<FakeImage*>(image_out.get())->image_info_.width, kImage2Width);
  ASSERT_EQ(image_pipe->fake_images_.size(), 2u);
  ASSERT_EQ(image_pipe->fake_images_[0]->update_count_, 0u);
  ASSERT_EQ(image_pipe->fake_images_[1]->update_count_, 1u);

  // Do it again, to make sure that update is called a second time (since released images could be
  // edited by the client before presentation).
  //
  // In this case, we need to run to idle after presenting image A, so that image B is returned by
  // the pool, marked dirty, and is free to be acquired again.
  image_pipe->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  image_pipe->PresentImage(kImageId2, zx::time(0), std::vector<zx::event>(),
                           std::vector<zx::event>(), nullptr);
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));

  image_out = image_pipe->current_image();
  ASSERT_EQ(image_pipe->fake_images_.size(), 2u);
  // Because Present was handled for image 1, we should have a call to
  // UpdatePixels for that image.
  ASSERT_EQ(image_pipe->fake_images_[0]->update_count_, 1u);
  ASSERT_EQ(image_pipe->fake_images_[1]->update_count_, 2u);
}

// Present two frames on the ImagePipe. After presenting the first image but before signaling its
// acquire fence, remove it. Verify that this doesn't cause any errors.
TEST_F(ImagePipe2Test, ImagePipeRemoveImageThatIsPendingPresent) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  // Add first image
  const uint32_t kImageId1 = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);

  zx::event acquire_fence1 = CreateEvent();
  zx::event release_fence1 = CreateEvent();
  image_pipe->PresentImage(kImageId1, zx::time(0), CopyEventIntoFidlArray(acquire_fence1),
                           CopyEventIntoFidlArray(release_fence1), nullptr);

  // Current presented image should be null, since we haven't signalled acquire fence yet.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());

  // Remove the image; by the ImagePipe semantics, the consumer will
  // still keep a reference to it so any future presents will still work.
  image_pipe->RemoveImage(kImageId1);

  // Signal on the acquire fence.
  acquire_fence1.signal(0u, escher::kFenceSignalled);

  // Run until image1 is presented.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));
  ASSERT_TRUE(image_pipe->current_image());
  ASSERT_FALSE(image_pipe->GetEscherImage());
  ImagePtr image1 = image_pipe->current_image();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  // Add second image
  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId, 1, image_format);
  RunLoopUntilIdle();

  // The first image should not have been released.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
  ASSERT_FALSE(IsEventSignalled(release_fence1, escher::kFenceSignalled));

  // Make gradient the currently displayed image.
  zx::event acquire_fence2 = CreateEvent();
  zx::event release_fence2 = CreateEvent();

  image_pipe->PresentImage(kImageId2, zx::time(0), CopyEventIntoFidlArray(acquire_fence2),
                           CopyEventIntoFidlArray(release_fence2), nullptr);

  // Verify that the currently display image hasn't changed yet, since we
  // haven't signalled the acquire fence.
  ASSERT_FALSE(RunLoopFor(zx::sec(1)));
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

// Detects protected memory backed image added.
TEST_F(ImagePipe2Test, DetectsProtectedMemory) {
  auto image_pipe = CreateImagePipe();
  auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);
  ASSERT_FALSE(image_pipe->use_protected_memory());

  image_pipe->set_next_image_is_protected(true);
  const uint32_t kImageId2 = 2;
  image_pipe->AddImage(kImageId2, kBufferId, 1, image_format);
  ASSERT_TRUE(image_pipe->use_protected_memory());

  image_pipe->RemoveImage(kImageId2);
  ASSERT_FALSE(image_pipe->use_protected_memory());

  EXPECT_ERROR_COUNT(0);
}

// Checks if NV12 and BGRAimage can be added.
TEST_F(ImagePipe2Test, AddMultipleFormatsImage) {
  auto image_pipe = CreateImagePipe();

  std::vector<fuchsia::sysmem::PixelFormatType> formats{fuchsia::sysmem::PixelFormatType::BGRA32,
                                                        fuchsia::sysmem::PixelFormatType::NV12};
  for (auto format : formats) {
    auto tokens = CreateSysmemTokens(image_pipe->sysmem_allocator(), true);
    const uint32_t kBufferId = 1;
    image_pipe->AddBufferCollection(kBufferId, std::move(tokens.local_token));

    const uint32_t kWidth = 32;
    const uint32_t kHeight = 32;
    const uint32_t kImageCount = 1;
    SetConstraints(image_pipe->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                   kImageCount, format, true, nullptr);

    fuchsia::sysmem::ImageFormat_2 image_format = {};
    image_format.coded_width = kWidth;
    image_format.coded_height = kHeight;
    const uint32_t kImageId1 = 1;
    image_pipe->AddImage(kImageId1, kBufferId, 0, image_format);
    EXPECT_EQ(format, image_pipe->pixel_format_);
    image_pipe->RemoveBufferCollection(kBufferId);
  }

  EXPECT_ERROR_COUNT(0);
}

// Detects not supported pixel format.

// TODO(23406): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace scenic_impl::gfx::test
