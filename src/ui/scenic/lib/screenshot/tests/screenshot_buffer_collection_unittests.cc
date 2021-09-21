// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "../screenshot.h"
#include "../screenshot_buffer_collection_importer.h"
#include "src/ui/lib/escher/test/common/gtest_escher.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/flatland/renderer/vk_renderer.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/utils/helpers.h"

using allocation::BufferCollectionImporter;
using fuchsia::sysmem::PixelFormatType;

namespace screenshot {
namespace test {

class ScreenshotBufferCollectionTest : public scenic_impl::gfx::test::VkSessionTest {
 public:
  void SetUp() override {
    VkSessionTest::SetUp();
    std::shared_ptr<flatland::VkRenderer> renderer =
        std::make_shared<flatland::VkRenderer>(escher()->GetWeakPtr());
    importer_ = std::make_unique<ScreenshotBufferCollectionImporter>(renderer);
  }

 protected:
  std::shared_ptr<ScreenshotBufferCollectionImporter> importer_;
};

class ScreenshotBCTestParameterized : public ScreenshotBufferCollectionTest,
                                      public testing::WithParamInterface<PixelFormatType> {};

INSTANTIATE_TEST_SUITE_P(, ScreenshotBCTestParameterized,
                         testing::Values(PixelFormatType::BGRA32, PixelFormatType::R8G8B8A8));

VK_TEST_F(ScreenshotBufferCollectionTest, ImportAndReleaseBufferCollection) {
  // Create Sysmem tokens.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator = utils::CreateSysmemAllocatorSyncPtr();
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
  bool result = importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(),
                                                  std::move(dup_token));
  EXPECT_TRUE(result);
  EXPECT_TRUE(result);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id);
}

VK_TEST_P(ScreenshotBCTestParameterized, ImportBufferImage) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  const auto pixel_format = GetParam();
  // Create Sysmem tokens.
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator = utils::CreateSysmemAllocatorSyncPtr();
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
  bool result = importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(),
                                                  std::move(dup_token));
  EXPECT_TRUE(result);

  // Set constraints.
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
  image_constraints.pixel_format.type = pixel_format;
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
  allocation::ImageMetadata metadata;
  metadata.width = kWidth;
  metadata.height = kHeight;
  metadata.vmo_index = 0;
  metadata.collection_id = collection_id;
  metadata.identifier = 1;

  // Verify image has been imported correctly.
  bool success = importer_->ImportBufferImage(metadata);
  EXPECT_TRUE(success);

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id);
}

VK_TEST_F(ScreenshotBufferCollectionTest, ImportBufferCollection_ErrorCases) {
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator = utils::CreateSysmemAllocatorSyncPtr();

  const auto collection_id = allocation::GenerateUniqueBufferCollectionId();
  fuchsia::sysmem::BufferCollectionTokenSyncPtr token1;
  zx_status_t status = sysmem_allocator->AllocateSharedCollection(token1.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  bool result =
      importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(token1));
  EXPECT_TRUE(result);

  // Buffer collection id dup.
  {
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token2;
    status = sysmem_allocator->AllocateSharedCollection(token2.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    result =
        importer_->ImportBufferCollection(collection_id, sysmem_allocator.get(), std::move(token2));
    EXPECT_FALSE(result);
  }

  // Buffer collection id mismatch.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = allocation::GenerateUniqueBufferCollectionId();
    result = importer_->ImportBufferImage(metadata);
    EXPECT_FALSE(result);
  }

  // Buffer collection id invalid.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = 0;
    result = importer_->ImportBufferImage(metadata);
    EXPECT_FALSE(result);
  }

  // Buffer collection has 0 width and height.
  {
    allocation::ImageMetadata metadata;
    metadata.collection_id = collection_id;
    metadata.width = 0;
    metadata.height = 0;
    result = importer_->ImportBufferImage(metadata);
    EXPECT_FALSE(result);
  }

  // Cleanup.
  importer_->ReleaseBufferCollection(collection_id);
}

}  // namespace test
}  // namespace screenshot
