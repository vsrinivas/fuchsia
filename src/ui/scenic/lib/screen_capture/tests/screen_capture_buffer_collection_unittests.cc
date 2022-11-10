// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <cstddef>

#include <gtest/gtest.h>

#include "../screen_capture.h"
#include "../screen_capture_buffer_collection_importer.h"
#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/id.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace screen_capture::test {

using allocation::BufferCollectionImporter;
using allocation::BufferCollectionUsage;
using fuchsia::sysmem::PixelFormatType;

class ScreenCaptureBufferCollectionTest : public scenic_impl::gfx::test::VkSessionTest {
 public:
  void SetUp() override {
    VkSessionTest::SetUp();
    renderer_ = std::make_shared<flatland::VkRenderer>(escher()->GetWeakPtr());
    importer_ = std::make_unique<ScreenCaptureBufferCollectionImporter>(
        utils::CreateSysmemAllocatorSyncPtr("SCBCTest::Setup"), renderer_,
        /*enable_copy_fallback=*/escher::test::GlobalEscherUsesVirtualGpu());
  }

  fuchsia::sysmem::BufferCollectionInfo_2 CreateBufferCollectionInfo2WithConstraints(
      fuchsia::sysmem::BufferCollectionConstraints constraints,
      allocation::GlobalBufferCollectionId collection_id) {
    zx_status_t status;
    fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
        utils::CreateSysmemAllocatorSyncPtr("CreateBCInfo2WithConstraints");
    // Create Sysmem tokens.

    auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator.get());

    // Import into ScreenCaptureBufferCollectionImporter.
    bool result = importer_->ImportBufferCollection(
        collection_id, sysmem_allocator.get(), std::move(dup_token),
        BufferCollectionUsage::kRenderTarget, std::nullopt);
    EXPECT_TRUE(result);

    fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
    status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                    buffer_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);

    status = buffer_collection->SetConstraints(true, constraints);
    EXPECT_EQ(status, ZX_OK);

    // Wait for allocation.
    zx_status_t allocation_status = ZX_OK;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
    status =
        buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
    status = buffer_collection->Close();
    EXPECT_EQ(status, ZX_OK);
    return buffer_collection_info;
  }

 protected:
  std::shared_ptr<flatland::VkRenderer> renderer_;
  std::shared_ptr<ScreenCaptureBufferCollectionImporter> importer_;
};

class ScreenCaptureBCTestParameterized : public ScreenCaptureBufferCollectionTest,
                                         public testing::WithParamInterface<PixelFormatType> {};

INSTANTIATE_TEST_SUITE_P(, ScreenCaptureBCTestParameterized,
                         testing::Values(PixelFormatType::BGRA32, PixelFormatType::R8G8B8A8));

VK_TEST_F(ScreenCaptureBufferCollectionTest, ImportAndReleaseBufferCollection) {
  // Create Sysmem tokens.
  zx_status_t status;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("SCBCTest-ImportAndReleaseBC");
  // Create Sysmem tokens.

  auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator.get());

  // Import into ScreenCaptureBufferCollectionImporter.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  bool result =
      importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(dup_token),
                                        BufferCollectionUsage::kRenderTarget, std::nullopt);

  EXPECT_TRUE(result);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

VK_TEST_P(ScreenCaptureBCTestParameterized, ImportBufferImage) {
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  // Set constraints.
  const auto pixel_format = GetParam();
  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t buffer_count = 2;
  fuchsia::sysmem::BufferCollectionConstraints constraints =
      utils::CreateDefaultConstraints(buffer_count, kWidth, kHeight);
  constraints.image_format_constraints[0].pixel_format.type = pixel_format;

  CreateBufferCollectionInfo2WithConstraints(constraints, collection_id);
  // Extract image into the first Session.
  allocation::ImageMetadata metadata;
  metadata.width = kWidth;
  metadata.height = kHeight;
  metadata.vmo_index = 0;
  metadata.collection_id = collection_id;
  metadata.identifier = 1;

  // Verify image has been imported correctly.
  bool success = importer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget);
  EXPECT_TRUE(success);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

VK_TEST_P(ScreenCaptureBCTestParameterized, GetBufferCountFromCollectionId) {
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  // Set constraints.
  const auto pixel_format = GetParam();
  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t buffer_count = 2;
  fuchsia::sysmem::BufferCollectionConstraints constraints =
      utils::CreateDefaultConstraints(buffer_count, kWidth, kHeight);
  constraints.image_format_constraints[0].pixel_format.type = pixel_format;

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfo2WithConstraints(constraints, collection_id);

  std::optional<uint32_t> info = importer_->GetBufferCollectionBufferCount(collection_id);

  EXPECT_NE(info, std::nullopt);
  EXPECT_EQ(info.value(), buffer_count);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

VK_TEST_F(ScreenCaptureBufferCollectionTest, ImportBufferCollection_ErrorCases) {
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("SCBCTest-ImportBC_ErrorCases");

  const auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token1;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(token1.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  bool result =
      importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(token1),
                                        BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  // Buffer collection id dup.
  {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token2;
    status = sysmem_allocator->AllocateSharedCollection(token2.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    result =
        importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(token2),
                                          BufferCollectionUsage::kRenderTarget, std::nullopt);
    EXPECT_FALSE(result);
  }
}

VK_TEST_P(ScreenCaptureBCTestParameterized, ImportBufferImage_ErrorCases) {
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  // Set constraints.
  const auto pixel_format = GetParam();
  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  const uint32_t buffer_count = 2;
  fuchsia::sysmem::BufferCollectionConstraints constraints =
      utils::CreateDefaultConstraints(buffer_count, kWidth, kHeight);
  constraints.image_format_constraints[0].pixel_format.type = pixel_format;

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfo2WithConstraints(constraints, collection_id);

  zx_status_t status;
  bool result;

  // Buffer collection id mismatch.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = allocation::GenerateUniqueBufferCollectionId();
    result = importer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget);
    EXPECT_FALSE(result);
  }

  // Buffer collection id invalid.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = 0;
    result = importer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget);
    EXPECT_FALSE(result);
  }

  // Buffer collection has 0 width and height.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = collection_id;
    metadata.width = 0;
    metadata.height = 0;
    result = importer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget);
    EXPECT_FALSE(result);
  }

  // Buffer count is does not correspond with vmo_index
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = collection_id;
    metadata.width = 32;
    metadata.height = 32;
    metadata.vmo_index = 3;
    result = importer_->ImportBufferImage(metadata, BufferCollectionUsage::kRenderTarget);
    EXPECT_FALSE(result);
  }

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

VK_TEST_P(ScreenCaptureBCTestParameterized, GetBufferCollectionBufferCount_ErrorCases) {
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  // Set constraints.
  const auto pixel_format = GetParam();
  const uint32_t kWidth = 0;
  const uint32_t kHeight = 0;
  const uint32_t buffer_count = 2;
  fuchsia::sysmem::BufferCollectionConstraints constraints =
      utils::CreateDefaultConstraints(buffer_count, kWidth, kHeight);
  constraints.image_format_constraints[0].pixel_format.type = pixel_format;

  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info =
      CreateBufferCollectionInfo2WithConstraints(constraints, collection_id);

  // collection_id does not exist
  {
    auto new_collection_id = allocation::GenerateUniqueBufferCollectionId();
    std::optional<uint32_t> info = importer_->GetBufferCollectionBufferCount(new_collection_id);
    EXPECT_EQ(info, std::nullopt);
  }

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

VK_TEST_P(ScreenCaptureBCTestParameterized, GetBufferCollectionBufferCount_BuffersNotAllocated) {
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  zx_status_t status;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("GetBCBC_BuffersNotAllocated");
  // Create Sysmem tokens.
  auto [local_token, dup_token] = utils::CreateSysmemTokens(sysmem_allocator.get());
  // Import into ScreenCaptureBufferCollectionImporter.
  bool result =
      importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(dup_token),
                                        BufferCollectionUsage::kRenderTarget, std::nullopt);
  EXPECT_TRUE(result);

  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);

  // CheckForBuffersAllocated will return false
  std::optional<uint32_t> info = importer_->GetBufferCollectionBufferCount(collection_id);
  EXPECT_EQ(info, std::nullopt);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id, BufferCollectionUsage::kRenderTarget);
}

}  // namespace screen_capture::test
