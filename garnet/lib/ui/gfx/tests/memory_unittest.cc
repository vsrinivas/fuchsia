// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/tests/session_test.h"
#include "garnet/lib/ui/gfx/tests/vk_session_test.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "gtest/gtest.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "lib/ui/scenic/cpp/commands.h"

using namespace escher;

namespace {

const uint32_t kVmoSize = 4096;
const uint32_t kMemoryId = 1;

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
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(
      memory_id, std::move(vmo), 0, fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError(
      "Memory::New(): allocation_size argument (0) is not valid.");

  // Re-create a vmo, and verify allocation size cannot be greater than
  // vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_FALSE(Apply(
      scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), kVmoSize + 1,
                                 fuchsia::images::MemoryType::HOST_MEMORY)));
  ExpectLastReportedError(
      "Memory::New(): allocation_size (4097) is larger than the size of the "
      "corresponding vmo (4096).");

  // Re-create a vmo, and verify allocation size can be < vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(
      memory_id, std::move(vmo), 1, fuchsia::images::MemoryType::HOST_MEMORY)));

  // Re-create a vmo, and verify allocation size can be == vmo_size.
  status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);
  memory_id++;
  ASSERT_TRUE(Apply(
      scenic::NewCreateMemoryCmd(memory_id, std::move(vmo), kVmoSize,
                                 fuchsia::images::MemoryType::HOST_MEMORY)));
}

VK_TEST_F(VkMemoryTest, ImportDeviceMemory) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  vk::MemoryRequirements requirements;
  requirements.size = kVmoSize;
  requirements.memoryTypeBits = 0xFFFFFFFF;

  // Create valid Vulkan device memory and import it into Scenic.
  auto memory =
      AllocateExportableMemory(device, physical_device, requirements,
                               vk::MemoryPropertyFlagBits::eDeviceLocal);
  zx::vmo device_vmo =
      ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);
  ASSERT_TRUE(Apply(scenic::NewCreateMemoryCmd(
      kMemoryId, std::move(device_vmo), kVmoSize,
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  // Confirm that the resource has a valid Vulkan memory object and cleanup.
  auto memory_resource = FindResource<Memory>(kMemoryId);
  ASSERT_TRUE(memory_resource->GetGpuMem());
  device.freeMemory(memory);
}

VK_TEST_F(VkMemoryTest, ImportReadOnlyHostMemory) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(kVmoSize, 0u, &vmo);
  ASSERT_EQ(ZX_OK, status);

  zx::vmo read_only;
  status = vmo.duplicate(ZX_RIGHT_READ | ZX_RIGHTS_BASIC, &read_only);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_TRUE(Apply(
      scenic::NewCreateMemoryCmd(kMemoryId, std::move(read_only), kVmoSize,
                                 fuchsia::images::MemoryType::HOST_MEMORY)));
  auto memory = FindResource<Memory>(kMemoryId);

  // Importing read-only host memory into the Vulkan driver should not work,
  // but it is not an error to try to do so.
  ASSERT_FALSE(memory->GetGpuMem());
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
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(
      kMemoryId, std::move(read_only), kVmoSize,
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): "
      "VkGetMemoryFuchsiaHandlePropertiesKHR returned zero valid memory "
      "types.");
}

VK_TEST_F(VkMemoryTest, ImportReadOnlyDeviceMemory) {
  auto vulkan_queues = CreateVulkanDeviceQueues();
  auto device = vulkan_queues->vk_device();
  auto physical_device = vulkan_queues->vk_physical_device();

  vk::MemoryRequirements requirements;
  requirements.size = kVmoSize;
  requirements.memoryTypeBits = 0xFFFFFFFF;

  auto memory =
      AllocateExportableMemory(device, physical_device, requirements,
                               vk::MemoryPropertyFlagBits::eDeviceLocal);
  zx::vmo device_vmo =
      ExportMemoryAsVmo(device, vulkan_queues->dispatch_loader(), memory);

  // This test creates valid device memory (unlike the previous test), but
  // still duplicates it, handing Scenic a read-only handle.
  //
  // TODO(MA-492): Fixing MA-492 would allow importation of read-only VMOs.
  zx::vmo read_only;
  zx_status_t status =
      device_vmo.duplicate(ZX_RIGHT_READ | ZX_RIGHTS_BASIC, &read_only);
  ASSERT_EQ(ZX_OK, status);

  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(
      kMemoryId, std::move(read_only), kVmoSize,
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): "
      "VkGetMemoryFuchsiaHandlePropertiesKHR returned zero valid memory "
      "types.");

  device.freeMemory(memory);
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
  ASSERT_FALSE(Apply(scenic::NewCreateMemoryCmd(
      kMemoryId, std::move(read_only), kVmoSize,
      fuchsia::images::MemoryType::VK_DEVICE_MEMORY)));

  ExpectLastReportedError(
      "scenic_impl::gfx::Memory::ImportGpuMemory(): "
      "VkGetMemoryFuchsiaHandlePropertiesKHR failed.");
}

}  // namespace test
}  // namespace gfx
}  // namespace scenic_impl
