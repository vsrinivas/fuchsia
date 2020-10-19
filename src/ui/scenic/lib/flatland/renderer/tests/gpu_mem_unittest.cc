// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/gpu_mem.h"

#include <lib/fdio/directory.h>

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_downloader.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/flatland/renderer/tests/common.h"

namespace {

vk::BufferCollectionFUCHSIA CreateVulkanCollection(const vk::Device& device,
                                                   const vk::DispatchLoaderDynamic& vk_loader,
                                                   flatland::BufferCollectionHandle token) {
  vk::BufferCollectionCreateInfoFUCHSIA buffer_collection_create_info;
  buffer_collection_create_info.collectionToken = token.TakeChannel().release();
  return escher::ESCHER_CHECKED_VK_RESULT(
      device.createBufferCollectionFUCHSIA(buffer_collection_create_info, nullptr, vk_loader));
}

}  // anonymous namespace

namespace escher {
namespace test {

const uint32_t kWidth = 32;
const uint32_t kHeight = 64;

using MemoryTest = flatland::RendererTest;

// Creates a buffer collection with multiple vmos and tries to import each of those
// vmos into GPU memory.
VK_TEST_F(MemoryTest, SimpleTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  const uint32_t kImageCount = 5U;

  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  vk::ImageUsageFlags usage = RectangleCompositor::kTextureUsageFlags;
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined, usage);

  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  auto result =
      flatland::BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_TRUE(result.is_ok());
  auto collection = std::move(result.value());

  // Set vulkan constraints.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                                     vulkan_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  auto vk_collection = CreateVulkanCollection(vk_device, vk_loader, std::move(vulkan_token));
  EXPECT_EQ(
      vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, image_create_info, vk_loader),
      vk::Result::eSuccess);

  // Set client constraints and wait for allocation.
  flatland::SetClientConstraintsAndWaitForAllocated(
      sysmem_allocator_.get(), std::move(tokens.local_token), kImageCount, kWidth, kHeight);

  // Server collection should be allocated now.
  EXPECT_TRUE(collection.BuffersAreAllocated());

  for (uint32_t i = 0; i < kImageCount; i++) {
    auto gpu_info = flatland::GpuImageInfo::New(vk_device, vk_loader, collection.GetSysmemInfo(),
                                                vk_collection, i);
    EXPECT_TRUE(gpu_info.GetGpuMem());
    EXPECT_TRUE(gpu_info.p_extension());
    auto vk_image_create_info = gpu_info.NewVkImageCreateInfo(kWidth, kHeight, usage);
    EXPECT_EQ(vk_image_create_info.extent, vk::Extent3D(kWidth, kHeight, 1));
    EXPECT_TRUE(vk_image_create_info.pNext);
  }

  // Cleanup.
  vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
}

// Even if the BufferCollection is valid and allocated, no memory should be allocated if an
// invalid index that is outside of the range of Vmos the BufferCollection has is provided.
VK_TEST_F(MemoryTest, OutOfBoundsTest) {
  const uint32_t kImageCount = 1U;

  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();

  vk::ImageUsageFlags usage = RectangleCompositor::kTextureUsageFlags;
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined, usage);

  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  auto result =
      flatland::BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
  EXPECT_TRUE(result.is_ok());
  auto collection = std::move(result.value());

  fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
  zx_status_t status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                                     vulkan_token.NewRequest());
  EXPECT_EQ(status, ZX_OK);
  auto vk_collection = CreateVulkanCollection(vk_device, vk_loader, std::move(vulkan_token));
  EXPECT_EQ(
      vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, image_create_info, vk_loader),
      vk::Result::eSuccess);

  flatland::SetClientConstraintsAndWaitForAllocated(
      sysmem_allocator_.get(), std::move(tokens.local_token), kImageCount, kWidth, kHeight);

  EXPECT_TRUE(collection.BuffersAreAllocated());

  // This should fail however, as the index is beyond bounds.
  auto gpu_info =
      flatland::GpuImageInfo::New(vk_device, vk_loader, collection.GetSysmemInfo(), vk_collection,
                                  /*index*/ 1);
  EXPECT_FALSE(gpu_info.GetGpuMem());

  // Cleanup.
  vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
}

// This test checks the entire pipeline flow, which involves creating a buffer
// collection, writing to one of its Vmos, creating the GPUInfo object, creating
// an image from that gpu object, and then finally reading out the pixel value
// from the image using the GPUDownloader and making sure the value matches what
// was written to the initial buffer.
VK_TEST_F(MemoryTest, ImageReadWriteTest) {
  SKIP_TEST_IF_ESCHER_USES_DEVICE(VirtualGpu);
  const uint32_t kImageCount = 1U;

  auto escher = GetEscher();
  auto vk_device = escher->vk_device();
  auto vk_loader = escher->device()->dispatch_loader();
  auto resource_recycler = escher->resource_recycler();
  vk::ImageUsageFlags usage = RectangleCompositor::kTextureUsageFlags;
  auto image_create_info =
      escher::RectangleCompositor::GetDefaultImageConstraints(vk::Format::eUndefined, usage);

  // First create the pair of sysmem tokens, one for the client, one for the server.
  auto tokens = flatland::CreateSysmemTokens(sysmem_allocator_.get());

  // Create the buffer collection struct and set the server-side vulkan constraints.
  flatland::BufferCollectionInfo server_collection;
  vk::BufferCollectionFUCHSIA vk_collection;
  {
    auto result =
        flatland::BufferCollectionInfo::New(sysmem_allocator_.get(), std::move(tokens.dup_token));
    EXPECT_TRUE(result.is_ok());
    server_collection = std::move(result.value());

    fuchsia::sysmem::BufferCollectionTokenSyncPtr vulkan_token;
    zx_status_t status = tokens.local_token->Duplicate(std::numeric_limits<uint32_t>::max(),
                                                       vulkan_token.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    vk_collection = CreateVulkanCollection(vk_device, vk_loader, std::move(vulkan_token));
    EXPECT_EQ(vk_device.setBufferCollectionConstraintsFUCHSIA(vk_collection, image_create_info,
                                                              vk_loader),
              vk::Result::eSuccess);
  }

  // Create a client-side handle to the buffer collection and set the client constraints.
  fuchsia::sysmem::BufferCollectionSyncPtr client_collection;
  {
    zx_status_t status = sysmem_allocator_->BindSharedCollection(std::move(tokens.local_token),
                                                                 client_collection.NewRequest());
    EXPECT_EQ(status, ZX_OK);
    fuchsia::sysmem::BufferCollectionConstraints constraints;
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints.cpu_domain_supported = true;
    constraints.buffer_memory_constraints.ram_domain_supported = true;
    constraints.usage.cpu = fuchsia::sysmem::cpuUsageWriteOften;
    constraints.min_buffer_count = kImageCount;

    constraints.image_format_constraints_count = 1;
    auto& image_constraints = constraints.image_format_constraints[0];
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] =
        fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

    image_constraints.required_min_coded_width = kWidth;
    image_constraints.required_min_coded_height = kHeight;
    image_constraints.required_max_coded_width = kWidth;
    image_constraints.required_max_coded_height = kHeight;
    image_constraints.max_coded_width = kWidth * 4;
    image_constraints.max_coded_height = kHeight;
    image_constraints.max_bytes_per_row = 0xffffffff;
    status = client_collection->SetConstraints(true, constraints);
    EXPECT_EQ(status, ZX_OK);
  }

  // Have the client wait for buffers allocated so it can populate its information
  // struct with the vmo data.
  fuchsia::sysmem::BufferCollectionInfo_2 client_collection_info = {};
  {
    zx_status_t allocation_status = ZX_OK;
    auto status =
        client_collection->WaitForBuffersAllocated(&allocation_status, &client_collection_info);
    EXPECT_EQ(status, ZX_OK);
    EXPECT_EQ(allocation_status, ZX_OK);
  }

  // Have the server also check allocation. Both client and server have set constraints, so this
  // should be true.
  EXPECT_TRUE(server_collection.BuffersAreAllocated());

  // Get a raw pointer from the client collection's vmo and write several values to it.
  uint8_t* vmo_host;
  const uint8_t kNumWrites = 10;
  const uint8_t kWriteValues[] = {200U, 150U, 93U, 50U, 80U, 77U, 11U, 32U, 9U, 199U};
  {
    const zx::vmo& image_vmo = client_collection_info.buffers[0].vmo;
    auto image_vmo_bytes = client_collection_info.settings.buffer_settings.size_bytes;
    ASSERT_TRUE(image_vmo_bytes > 0);
    auto status = zx::vmar::root_self()->map(ZX_VM_PERM_WRITE | ZX_VM_PERM_READ,
                                             0, image_vmo, 0, image_vmo_bytes,
                                             reinterpret_cast<uintptr_t*>(&vmo_host));
    EXPECT_EQ(status, ZX_OK);
    memcpy(vmo_host, kWriteValues, sizeof(kWriteValues));
  }

  // Create the GPU info from the server side collection.
  auto gpu_info = flatland::GpuImageInfo::New(
      vk_device, vk_loader, server_collection.GetSysmemInfo(), vk_collection, /*index*/ 0);
  EXPECT_TRUE(gpu_info.GetGpuMem());

  // Create an image from the server side collection.
  auto image =
      image_utils::NewImage(vk_device, gpu_info.NewVkImageCreateInfo(kWidth, kHeight, usage),
                            gpu_info.GetGpuMem(), resource_recycler);

  // The returned image should not be null and should have the
  // width and height specified above.
  EXPECT_TRUE(image);
  if (image) {
    EXPECT_EQ(image->width(), kWidth);
    EXPECT_EQ(image->height(), kHeight);
    EXPECT_EQ(image->vk_format(), vk::Format::eB8G8R8A8Unorm);
    EXPECT_EQ(image->size(), kWidth * kHeight * 4);
  }

  // Now we will read from the image and see if it matches what we wrote to it on the client side.
  BatchGpuDownloader downloader(escher->GetWeakPtr(), CommandBuffer::Type::kGraphics, 0);
  bool read_image_done = false;
  downloader.ScheduleReadImage(
      image, [&read_image_done, &kWriteValues](const void* host_ptr, size_t size) {
        for (uint32_t i = 0; i < kNumWrites; i++) {
          EXPECT_EQ(static_cast<const uint8_t*>(host_ptr)[i], kWriteValues[i]);
        }
        read_image_done = true;
      });

  bool batch_download_done = false;
  downloader.Submit([&batch_download_done]() { batch_download_done = true; });

  escher->vk_device().waitIdle();
  EXPECT_TRUE(escher->Cleanup());
  EXPECT_TRUE(read_image_done);
  EXPECT_TRUE(batch_download_done);
  vk_device.destroyBufferCollectionFUCHSIA(vk_collection, nullptr, vk_loader);
}

}  // namespace test
}  // namespace escher
