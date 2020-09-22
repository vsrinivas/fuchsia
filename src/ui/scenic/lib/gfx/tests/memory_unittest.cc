// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/ui/scenic/cpp/commands.h>

#include <gtest/gtest.h>

#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/gfx/tests/session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_session_test.h"
#include "src/ui/scenic/lib/gfx/tests/vk_util.h"

using namespace escher;

namespace {

const uint32_t kVmoSize = 4096;
const uint32_t kMemoryId = 1;

vk::Image CreateSingleRowDeviceVkImageOfWidth(vk::Device device, uint32_t width,
                                              vk::Format format) {
  escher::ImageInfo info = {
      .format = format,
      .width = width,
      .height = 1,
      .sample_count = 1,
      .usage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled,
      .memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal,
      .tiling = vk::ImageTiling::eOptimal,
      .is_external = true};
  return escher::image_utils::CreateVkImage(device, info, vk::ImageLayout::eUndefined);
}

}  // namespace

namespace scenic_impl {
namespace gfx {
namespace test {

using MemoryTest = SessionTest;
using VkMemoryTest = VkSessionTest;

// Creates a memory object and verifies that the allocation size validation
// logic is working.
TEST_F(MemoryTest, MemoryAllocationSizeValidation) {
  zx::vmo vmo;

  // Create a vmo, and verify allocation size cannot be 0.
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  uint32_t memory_id = 1;
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), 0,
                                                fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError("Memory::New(): allocation_size argument (0) is not valid.");

  // Re-create a vmo, and verify allocation size cannot be greater than
  // vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), kVmoSize + 1,
                                                fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError(
      "Memory::New(): allocation_size (4097) is larger than the size of the "
      "corresponding vmo (4096).");

  // Re-create a vmo, and verify allocation size can be < vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), 1,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));

  // Re-create a vmo, and verify allocation size can be == vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));
}

VK_TEST_F(VkMemoryTest, ImportDeviceMemory) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // Create an VkImage and allocate exportable memory for that image.
  //
  // |AllocateExportableMemoryDedicatedToImageIfRequired()| will allocate an
  // image-dedicated memory only if it is required, otherwise it will allocate
  // non dedicated memory instead.
  vk::Image image = CreateSingleRowDeviceVkImageOfWidth(device, /* width */ kVmoSize / 4,
                                                        vk::Format::eR8G8B8A8Srgb);
  MemoryAllocationResult allocation_result = AllocateExportableMemoryDedicatedToImageIfRequired(
      device, physical_device, kVmoSize, image, vk::MemoryPropertyFlagBits::eDeviceLocal,
      vulkan_queues->dispatch_loader());
  vk::DeviceMemory memory = allocation_result.device_memory;
  // Import valid Vulkan device memory into Scenic.
  zx::vmo device_vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);
  size_t vmo_size = 0u;
  ASSERT_TRUE(device_vmo.get_size(&vmo_size) == ZX_OK);
  ASSERT_GE(vmo_size, kVmoSize);
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(device_vmo), vmo_size,
                                               fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  // Confirm that the resource has a valid Vulkan memory object and cleanup.
  auto memory_resource = FindResource<Memory>(kMemoryId);
  ASSERT_TRUE(memory_resource->GetGpuMem(session()->error_reporter()));
  device.freeMemory(memory);
  device.destroyImage(image);
}

VK_TEST_F(VkMemoryTest, ImportReadOnlyHostMemory) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  zx::vmo read_only;
  status = vmo.duplicate(ZX_RIGHT_READ | ZX_RIGHTS_BASIC, &read_only);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(read_only), kVmoSize,
                                               fuchsia::images::MemoryType::HOST_MEMORY)));
  auto memory = FindResource<Memory>(kMemoryId);

  // Importing read-only host memory into the Vulkan driver should not work,
  // but it is not an error to try to do so.
  ASSERT_FALSE(memory->GetGpuMem(session()->error_reporter()));
  ExpectLastReportedError(nullptr);
}  // namespace gfx

VK_TEST_F(VkMemoryTest, ImportReadOnlyHostMemoryAsDeviceMemory) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  zx::vmo read_only;
  status = vmo.duplicate(ZX_RIGHT_READ | ZX_RIGHTS_BASIC, &read_only);
  ASSERT_EQ(ZX_OK, status);

  // This client lies to Scenic, stating that is importing device memory when
  // it has only created a read-only host memory VMO.
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(read_only), kVmoSize,
                                                fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): VMO doesn't have right ZX_RIGHT_WRITE");
}

VK_TEST_F(VkMemoryTest, ImportReadOnlyDeviceMemory) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // Create an VkImage and allocate exportable memory for that image.
  //
  // |AllocateExportableMemoryDedicatedToImageIfRequired()| will allocate an
  // image-dedicated memory only if it is required, otherwise it will allocate
  // non dedicated memory instead.
  vk::Image image = CreateSingleRowDeviceVkImageOfWidth(device, /* width */ kVmoSize / 4,
                                                        vk::Format::eR8G8B8A8Srgb);
  MemoryAllocationResult allocation_result = AllocateExportableMemoryDedicatedToImageIfRequired(
      device, physical_device, kVmoSize, image, vk::MemoryPropertyFlagBits::eDeviceLocal,
      vulkan_queues->dispatch_loader());
  vk::DeviceMemory memory = allocation_result.device_memory;
  // Import valid Vulkan device memory into Scenic.
  zx::vmo device_vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);
  // This test creates valid device memory (unlike the previous test), but
  // still duplicates it, handing Scenic a read-only handle.
  //
  // TODO(fxbug.dev/13100): Fixing MA-492 would allow importation of read-only VMOs.
  zx::vmo read_only;
  zx_status_t status = device_vmo.duplicate(
      ZX_RIGHT_READ | ZX_RIGHT_TRANSFER | ZX_RIGHT_DUPLICATE | ZX_RIGHT_WAIT, &read_only);
  ASSERT_EQ(ZX_OK, status);

  size_t vmo_size = 0u;
  ASSERT_TRUE(device_vmo.get_size(&vmo_size) == ZX_OK);
  ASSERT_GE(vmo_size, kVmoSize);

  // Currently vulkan driver of AEMU supports importing read-only device VMOs
  // while magma lib doesn't support that since it cannot get memory types of a
  // read-only vmo.
  // Therefore, we require all VMOs to have read and write rights.
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(read_only), vmo_size,
                                                fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): VMO doesn't have right ZX_RIGHT_WRITE");

  device.freeMemory(memory);
  device.destroyImage(image);
}

VK_TEST_F(VkMemoryTest, ImportUsingVkMemoryAllocateInfo) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  // Create an VkImage and allocate exportable memory for that image.
  //
  // |AllocateExportableMemoryDedicatedToImageIfRequired()| will allocate an
  // image-dedicated memory only if it is required, otherwise it will allocate
  // non dedicated memory instead.
  vk::Image image = CreateSingleRowDeviceVkImageOfWidth(device, /* width */ kVmoSize / 4,
                                                        vk::Format::eR8G8B8A8Srgb);
  MemoryAllocationResult allocation_result = AllocateExportableMemoryDedicatedToImageIfRequired(
      device, physical_device, kVmoSize, image, vk::MemoryPropertyFlagBits::eDeviceLocal,
      vulkan_queues->dispatch_loader());
  vk::DeviceMemory memory = allocation_result.device_memory;
  // Import valid Vulkan device memory into Scenic.
  zx::vmo device_vmo = ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  // Fill vk::MemoryAllocateInfo
  zx::vmo clone_vmo;
  zx_status_t status = device_vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &clone_vmo);
  ASSERT_EQ(ZX_OK, status);
  auto import_info = vk::ImportMemoryZirconHandleInfoFUCHSIA(
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA, clone_vmo.release());

  // Here we get the memory type we set in function VkSessionTest::
  // AllocateExportableMemoryDedicatedToImageIfRequired.
  //
  // For dedicated allocation, we use memory type required by the image
  // allocation dedicated to; for non-dedicated allocation, we use 0xFFFFFFFF to
  // represent *any*  possible memory type supported by device as long as they
  // support the memory property flags defined above.
  uint32_t memory_type_bits = allocation_result.is_dedicated
                                  ? device.getImageMemoryRequirements(image).memoryTypeBits
                                  : 0xFFFFFFFF;
  auto alloc_info = vk::MemoryAllocateInfo(
      allocation_result.size, escher::impl::GetMemoryTypeIndex(physical_device, memory_type_bits,
                                                               vk::MemoryPropertyFlags()));
  alloc_info.setPNext(&import_info);
  auto memory_resource = Memory::New(session(), kMemoryId, std::move(device_vmo), alloc_info,
                                     session()->shared_error_reporter().get());

  // Confirm that the resource has a valid Vulkan memory object and cleanup.
  ASSERT_TRUE(memory_resource->GetGpuMem(session()->error_reporter()));
  device.freeMemory(memory);
  device.destroyImage(image);
}

VK_TEST_F(VkMemoryTest, ImportMaliciousClient) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  zx::vmo read_only;
  // This vmo can't be duplicated or transferred. But Scenic happens to be in
  // the same process as this test. So the first system that will fail on the
  // limited-use handle will be the Vulkan driver, and Scenic is expected to
  // recover cleanly.
  status = vmo.duplicate(ZX_RIGHT_READ, &read_only);
  ASSERT_EQ(ZX_OK, status);

  // This client lies to Scenic, stating that is importing device memory when
  // it has only created a read-only host memory VMO.
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(kMemoryId, std::move(read_only), kVmoSize,
                                                fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));
  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): VMO doesn't have right ZX_RIGHT_WRITE");
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
