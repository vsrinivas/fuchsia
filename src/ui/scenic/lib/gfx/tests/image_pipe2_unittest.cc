// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image_pipe2.h"

#include <lib/fdio/directory.h>
#include <lib/ui/scenic/cpp/commands.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/image_pipe_updater.h"
#include "src/ui/scenic/lib/gfx/tests/error_reporting_test.h"
#include "src/ui/scenic/lib/gfx/tests/image_pipe_unittest_common.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/mocks.h"
#include "src/ui/scenic/lib/gfx/tests/mocks/util.h"
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
      case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
        image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
        break;
      case fuchsia::sysmem::PixelFormatType::I420:
      case fuchsia::sysmem::PixelFormatType::NV12:
        image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
        break;
      default:
        FX_NOTREACHED();
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
  ImagePipe2ThatCreatesFakeImages(gfx::Session* session, std::unique_ptr<ImagePipeUpdater> updater,
                                  fidl::InterfaceRequest<fuchsia::images::ImagePipe2> request,
                                  escher::ResourceManager* fake_resource_manager)
      : ImagePipe2(session, 0u, std::move(request), std::move(updater),
                   session->shared_error_reporter()),
        fake_resource_manager_(fake_resource_manager) {
    zx_status_t status = fdio_service_connect(
        "/svc/fuchsia.sysmem.Allocator", sysmem_allocator_.NewRequest().TakeChannel().release());
    EXPECT_EQ(status, ZX_OK);
  }
  ~ImagePipe2ThatCreatesFakeImages() { CloseConnectionAndCleanUp(); }

  ImagePipeUpdateResults Update(scheduling::PresentId present_id) override {
    auto result = ImagePipe2::Update(present_id);
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
    FX_CHECK(escher_image);
    auto image = fxl::AdoptRef(new FakeImage(session, image_id, escher_image));
    fake_images_.push_back(image);
    return image;
  }

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
  escher::ResourceManager* fake_resource_manager_;
  bool next_image_is_protected_ = false;
};

// Creates test environment.
class ImagePipe2Test : public ErrorReportingTest, public escher::ResourceManager {
 public:
  ImagePipe2Test() : escher::ResourceManager(escher::EscherWeakPtr()) {}

  void OnReceiveOwnable(std::unique_ptr<escher::Resource> resource) override {}

  void SetUp() override {
    ErrorReportingTest::SetUp();

    gfx_session_ = std::make_unique<gfx::Session>(/*id=*/1, SessionContext{},
                                                  shared_event_reporter(), shared_error_reporter());
    auto updater = std::make_unique<MockImagePipeUpdater>();
    image_pipe_updater_ = updater.get();
    image_pipe_ = fxl::MakeRefCounted<ImagePipe2ThatCreatesFakeImages>(
        gfx_session_.get(), std::move(updater), image_pipe_handle_.NewRequest(), this);
  }

  void TearDown() override {
    image_pipe_.reset();
    image_pipe_updater_ = nullptr;
    gfx_session_.reset();

    ErrorReportingTest::TearDown();
  }

  fxl::RefPtr<ImagePipe2ThatCreatesFakeImages> image_pipe_;
  MockImagePipeUpdater* image_pipe_updater_;

 private:
  std::unique_ptr<gfx::Session> gfx_session_;
  fidl::InterfacePtr<fuchsia::images::ImagePipe2> image_pipe_handle_;
};

// Present a BufferCollection with an Id of zero, and expect an error.
TEST_F(ImagePipe2Test, BufferCollectionIdMustNotBeZero) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), false);

  const uint32_t kBufferId = 0;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  ExpectLastReportedError("AddBufferCollection: BufferCollection can not be assigned an ID of 0.");
}

// Present an image with an Id of zero, and expect an error.
TEST_F(ImagePipe2Test, ImagePipeImageIdMustNotBeZero) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  // So that at least one participant is specifying a non-zero minimum / needed buffer size.
  const uint32_t kWidth = 2;
  const uint32_t kHeight = 2;
  const uint32_t kImageCount = 1;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, false, nullptr);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kImageId = 0;
  image_pipe_->AddImage(kImageId, kBufferId, 0, fuchsia::sysmem::ImageFormat_2());

  ExpectLastReportedError("AddImage: Image can not be assigned an ID of 0.");
}

// Add multiple images from same buffer collection.
TEST_F(ImagePipe2Test, AddMultipleImagesFromABufferCollection) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId, 1, image_format);

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Add multiple images from an invalid buffer collection id.
TEST_F(ImagePipe2Test, BufferCollectionIdMustBeValid) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId + 1, 1, image_format);

  ExpectLastReportedError("AddImage: resource with ID not found.");
}

// Add multiple images from same buffer collection.
TEST_F(ImagePipe2Test, BufferCollectionIndexMustBeValid) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId, kImageCount, image_format);

  ExpectLastReportedError("AddImage: buffer_collection_index out of bounds");
}

// Removing buffer collection removes associated images.
TEST_F(ImagePipe2Test, RemoveBufferCollectionRemovesImages) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  image_pipe_->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                            std::vector<zx::event>(), /*callback=*/[](auto) {});

  // Remove buffer collection
  image_pipe_->RemoveBufferCollection(kBufferId);
  image_pipe_->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                            std::vector<zx::event>(), /*callback=*/[](auto) {});

  ExpectLastReportedError("PresentImage: could not find Image with ID: 1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipe2Test, PresentImage_ShouldCallScheduleUpdate) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe_->AddImage(kImageId, kBufferId, 0, image_format);

  EXPECT_EQ(image_pipe_updater_->schedule_update_call_count_, 0u);

  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_EQ(image_pipe_updater_->schedule_update_call_count_, 1u);

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Call Present with out-of-order presentation times, and expect an error.
TEST_F(ImagePipe2Test, PresentImagesOutOfOrder) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe_->AddImage(kImageId, kBufferId, 0, image_format);

  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(kImageId, zx::time(0), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  ExpectLastReportedError(
      "PresentImage: Present called with out-of-order presentation "
      "time. presentation_time=0, last scheduled presentation time=1");
}

// Call Present with in-order presentation times, and expect no error.
TEST_F(ImagePipe2Test, PresentImagesInOrder) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe_->AddImage(kImageId, kBufferId, 0, image_format);

  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Call Present with an image with an odd size(possible offset) into its memory, and expect no
// error.
TEST_F(ImagePipe2Test, PresentImagesWithOddSize) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 35;
  const uint32_t kHeight = 35;
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight, 1u,
                 fuchsia::sysmem::PixelFormatType::BGRA32, true, &buffer_collection);

  const uint32_t kImageId = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe_->AddImage(kImageId, kBufferId, 0, image_format);

  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});
  image_pipe_->PresentImage(kImageId, zx::time(1), CopyEventIntoFidlArray(CreateEvent()),
                            CopyEventIntoFidlArray(CreateEvent()), /*callback=*/[](auto) {});

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Present two frames on the ImagePipe, making sure that both buffers are allocated, and that both
// are updated with their respective Update calls.
TEST_F(ImagePipe2Test, ImagePipePresentTwoFrames) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  const auto present_id =
      image_pipe_->PresentImage(kImageId1, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Current presented image should be null, since we haven't called Update yet.
  ASSERT_FALSE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  image_pipe_->Update(present_id);
  ASSERT_TRUE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  // Image should now be presented.
  ImagePtr image1 = image_pipe_->current_image();
  ASSERT_TRUE(image1);

  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId, 1, image_format);

  const auto present_id2 =
      image_pipe_->PresentImage(kImageId2, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Verify that the currently display image hasn't changed yet, since we
  // haven't called Update yet.
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_EQ(image_pipe_->current_image(), image1);

  image_pipe_->Update(present_id2);

  // There should be a new image presented.
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ImagePtr image2 = image_pipe_->current_image();
  ASSERT_TRUE(image2);
  ASSERT_NE(image1, image2);
}

// Present two frames on the ImagePipe and skip one, making sure that UpdatePixels is only called on
// images that are used.
TEST_F(ImagePipe2Test, ImagePipeUpdateTwoFrames) {
  // Add first image 32x32
  auto tokens1 = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBuffer1Id = 1;
  image_pipe_->AddBufferCollection(kBuffer1Id, std::move(tokens1.local_token));

  const uint32_t kImage1Width = 32;
  const uint32_t kImage1Height = 32;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens1.dup_token), kImage1Width,
                 kImage1Height, 1u, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format1 = {};
  image_format1.coded_width = kImage1Width;
  image_format1.coded_height = kImage1Height;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBuffer1Id, 0, image_format1);

  // Add second image 48x48
  auto tokens2 = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBuffer2Id = 2;
  image_pipe_->AddBufferCollection(kBuffer2Id, std::move(tokens2.local_token));

  const uint32_t kImage2Width = 48;
  const uint32_t kImage2Height = 48;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens2.dup_token), kImage2Width,
                 kImage2Height, 1u, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format_2 = {};
  image_format_2.coded_width = kImage2Width;
  image_format_2.coded_height = kImage2Height;
  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBuffer2Id, 0, image_format_2);

  // Present both images
  image_pipe_->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                            std::vector<zx::event>(), /*callback=*/[](auto) {});
  const auto present_id =
      image_pipe_->PresentImage(kImageId2, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});

  image_pipe_->Update(present_id);

  auto image_out = image_pipe_->current_image();
  // We should get the second image in the queue, since both should have been ready.
  ASSERT_TRUE(image_out);
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_EQ(static_cast<FakeImage*>(image_out.get())->image_info_.width, kImage2Width);
  ASSERT_EQ(image_pipe_->fake_images_.size(), 2u);
  ASSERT_EQ(image_pipe_->fake_images_[0]->update_count_, 0u);
  ASSERT_EQ(image_pipe_->fake_images_[1]->update_count_, 1u);

  // Do it again, to make sure that update is called a second time (since released images could be
  // edited by the client before presentation).
  //
  // In this case, we need to run to idle after presenting image A, so that image B is returned by
  // the pool, marked dirty, and is free to be acquired again.
  const auto present_id2 =
      image_pipe_->PresentImage(kImageId1, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});
  image_pipe_->Update(present_id2);
  const auto present_id3 =
      image_pipe_->PresentImage(kImageId2, zx::time(0), std::vector<zx::event>(),
                                std::vector<zx::event>(), /*callback=*/[](auto) {});
  image_pipe_->Update(present_id3);

  image_out = image_pipe_->current_image();
  ASSERT_EQ(image_pipe_->fake_images_.size(), 2u);
  // Because Present was handled for image 1, we should have a call to
  // UpdatePixels for that image.
  ASSERT_EQ(image_pipe_->fake_images_[0]->update_count_, 1u);
  ASSERT_EQ(image_pipe_->fake_images_[1]->update_count_, 2u);
}

// Present two frames on the ImagePipe. After presenting the first image but before signaling its
// acquire fence, remove it. Verify that this doesn't cause any errors.
TEST_F(ImagePipe2Test, ImagePipeRemoveImageThatIsPendingPresent) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  // Add first image
  const uint32_t kImageId1 = 1;
  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);

  const auto present_id =
      image_pipe_->PresentImage(kImageId1, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Current presented image should be null, since we haven't called Update yet.
  ASSERT_FALSE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());

  // Remove the image; by the ImagePipe semantics, the consumer will
  // still keep a reference to it so any future presents will still work.
  image_pipe_->RemoveImage(kImageId1);

  // Update to image1.
  image_pipe_->Update(present_id);
  ASSERT_TRUE(image_pipe_->current_image());
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ImagePtr image1 = image_pipe_->current_image();

  // Image should now be presented.
  ASSERT_TRUE(image1);

  // Add second image
  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId, 1, image_format);

  // Make gradient the currently displayed image.
  const auto present_id2 =
      image_pipe_->PresentImage(kImageId2, zx::time(0), /*acquire_fences=*/{},
                                /*release_fences=*/{}, /*callback=*/[](auto) {});

  // Verify that the currently display image hasn't changed yet, since we haven't called Update yet.
  ASSERT_EQ(image_pipe_->current_image(), image1);

  // Update to image2.
  image_pipe_->Update(present_id2);

  // There should be a new image presented.
  ImagePtr image2 = image_pipe_->current_image();
  ASSERT_TRUE(image2);
  ASSERT_FALSE(image_pipe_->GetEscherImage());
  ASSERT_NE(image1, image2);
  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Detects protected memory backed image added.
TEST_F(ImagePipe2Test, DetectsProtectedMemory) {
  auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);

  const uint32_t kBufferId = 1;
  image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t kImageCount = 2;
  SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                 kImageCount, fuchsia::sysmem::PixelFormatType::BGRA32, true, nullptr);

  fuchsia::sysmem::ImageFormat_2 image_format = {};
  image_format.coded_width = kWidth;
  image_format.coded_height = kHeight;
  const uint32_t kImageId1 = 1;
  image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);
  ASSERT_FALSE(image_pipe_->use_protected_memory());

  image_pipe_->set_next_image_is_protected(true);
  const uint32_t kImageId2 = 2;
  image_pipe_->AddImage(kImageId2, kBufferId, 1, image_format);
  ASSERT_TRUE(image_pipe_->use_protected_memory());

  image_pipe_->RemoveImage(kImageId2);
  ASSERT_FALSE(image_pipe_->use_protected_memory());

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// Checks all supported pixel formats can be added.
TEST_F(ImagePipe2Test, SupportsMultiplePixelFormats) {
  std::vector<fuchsia::sysmem::PixelFormatType> formats{
      fuchsia::sysmem::PixelFormatType::BGRA32, fuchsia::sysmem::PixelFormatType::I420,
      fuchsia::sysmem::PixelFormatType::NV12, fuchsia::sysmem::PixelFormatType::R8G8B8A8};
  for (auto format : formats) {
    auto tokens = CreateSysmemTokens(image_pipe_->sysmem_allocator(), true);
    const uint32_t kBufferId = 1;
    image_pipe_->AddBufferCollection(kBufferId, std::move(tokens.local_token));

    const uint32_t kWidth = 32;
    const uint32_t kHeight = 32;
    const uint32_t kImageCount = 1;
    SetConstraints(image_pipe_->sysmem_allocator(), std::move(tokens.dup_token), kWidth, kHeight,
                   kImageCount, format, true, nullptr);

    fuchsia::sysmem::ImageFormat_2 image_format = {};
    image_format.coded_width = kWidth;
    image_format.coded_height = kHeight;
    const uint32_t kImageId1 = 1;
    image_pipe_->AddImage(kImageId1, kBufferId, 0, image_format);
    EXPECT_EQ(format, image_pipe_->pixel_format_);
    image_pipe_->RemoveBufferCollection(kBufferId);
  }

  EXPECT_SCENIC_SESSION_ERROR_COUNT(0);
}

// TODO(fxbug.dev/23406): More tests.
// - Test that you can't add the same image twice.
// - Test that you can't present an image that doesn't exist.
// - Test what happens when an acquire fence is closed on the client end.
// - Test what happens if you present an image twice.

}  // namespace scenic_impl::gfx::test
