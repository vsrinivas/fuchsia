// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/gfx_buffer_collection_importer.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/fdio/directory.h>

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace scenic_impl {
namespace gfx {
namespace test {

class GfxBufferCollectionImporterTest : public VkSessionTest {
 public:
  void SetUp() override {
    VkSessionTest::SetUp();
    importer_ = std::make_unique<GfxBufferCollectionImporter>(escher()->GetWeakPtr());
  }

 protected:
  std::shared_ptr<GfxBufferCollectionImporter> importer_;
};

VK_TEST_F(GfxBufferCollectionImporterTest, ImportBufferCollection) {
  // Create Sysmem tokens.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("GfxBufferCollectionImporterTest");
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);

  // Import into GfxBufferCollectionImporter.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  bool result = importer_->ImportBufferCollection(
      collection_id, sysmem_allocator.get(), std::move(dup_token),
      allocation::BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id,
                                     allocation::BufferCollectionUsage::kClientImage);
}

VK_TEST_F(GfxBufferCollectionImporterTest, ExtractImageForMultipleSessions) {
  // Create Sysmem tokens.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("GfxBufferCollectionImporterTest");
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(local_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  status = local_token->Sync();
  EXPECT_EQ(status, ZX_OK);

  // Import into GfxBufferCollectionImporter.
  auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  bool result = importer_->ImportBufferCollection(
      collection_id, sysmem_allocator.get(), std::move(dup_token),
      allocation::BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  // Set constraints, including width and height which isn't specified by
  // GfxBufferCollectionImporter.
  const uint32_t kWidth = 32;
  const uint32_t kHeight = 32;
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  status = sysmem_allocator->BindSharedCollection(std::move(local_token),
                                                  buffer_collection.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;
  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.min_coded_width = kWidth;
  image_constraints.max_coded_width = kWidth;
  image_constraints.min_coded_height = kHeight;
  image_constraints.max_coded_height = kHeight;
  status = buffer_collection->SetConstraints(true, constraints);
  EXPECT_EQ(status, ZX_OK);

  // Wait for allocation.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(allocation_status, ZX_OK);
  status = buffer_collection->Close();
  EXPECT_EQ(status, ZX_OK);

  // Extract image into the first Session.
  allocation::ImageMetadata metadata1;
  metadata1.width = kWidth;
  metadata1.height = kHeight;
  metadata1.vmo_index = 0;
  metadata1.collection_id = collection_id;
  // Extract image and expect it be set.
  auto image1 = importer_->ExtractImage(session(), metadata1, 123);
  EXPECT_TRUE(image1.get());

  // Export same image using another Session.
  auto session2 = CreateSession();
  allocation::ImageMetadata metadata2;
  metadata2.width = kWidth;
  metadata2.height = kHeight;
  metadata2.vmo_index = 0;
  metadata2.collection_id = collection_id;
  auto image2 = importer_->ExtractImage(session2.get(), metadata2, 123);
  EXPECT_TRUE(image2.get());

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id,
                                     allocation::BufferCollectionUsage::kClientImage);
}

VK_TEST_F(GfxBufferCollectionImporterTest, ErrorCases) {
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator =
      utils::CreateSysmemAllocatorSyncPtr("GfxBufferCollectionImporterTest");

  const auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token1;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(token1.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  bool result = importer_->ImportBufferCollection(
      collection_id, sysmem_allocator.get(), std::move(token1),
      allocation::BufferCollectionUsage::kClientImage, std::nullopt);
  EXPECT_TRUE(result);

  // Buffer collection id dup.
  {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token2;
    status = sysmem_allocator->AllocateSharedCollection(token2.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    result = importer_->ImportBufferCollection(
        collection_id, sysmem_allocator.get(), std::move(token2),
        allocation::BufferCollectionUsage::kClientImage, std::nullopt);
    EXPECT_FALSE(result);
  }

  // Buffer collection id mismatch.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = allocation::GenerateUniqueBufferCollectionId();
    auto image = importer_->ExtractImage(nullptr, metadata, 123);
    EXPECT_FALSE(image.get());
  }

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id,
                                     allocation::BufferCollectionUsage::kClientImage);
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
