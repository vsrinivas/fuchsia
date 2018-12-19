#include "gtest/gtest.h"
#include "lib/escher/test/fake_gpu_allocator.h"
#include "lib/escher/test/gtest_vulkan.h"
#include "lib/escher/vk/naive_gpu_allocator.h"
#include "lib/escher/vk/vma_gpu_allocator.h"
#include "lib/escher/vk/vulkan_context.h"
#include "lib/escher/vk/vulkan_device_queues.h"

namespace {
using namespace escher;
using namespace escher::test;

// vk_mem_alloc allocates power of 2 buffers by default, so this makes the
// tests easier to verify.
const vk::DeviceSize kMemorySize = 1024;

void TestAllocationOfMemory(GpuAllocator* allocator) {
  // Standard sub-allocation tests.
  auto alloc = allocator->AllocateMemory(
      {kMemorySize, 0, 0xffffffff}, vk::MemoryPropertyFlagBits::eDeviceLocal);

  // Adding sub-allocations doesn't increase slab-count.
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  auto sub_alloc1 = alloc->Suballocate(kMemorySize, 0);
  auto sub_alloc1a = sub_alloc1->Suballocate(kMemorySize, 0);
  auto sub_alloc1b = sub_alloc1->Suballocate(kMemorySize, 0);
  auto sub_alloc2 = alloc->Suballocate(kMemorySize, 0);
  auto sub_alloc2a = sub_alloc2->Suballocate(kMemorySize, 0);
  auto sub_alloc2b = sub_alloc2->Suballocate(kMemorySize, 0);
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocating then freeing increases/decreases the slab-count.
  auto alloc2 = allocator->AllocateMemory(
      {kMemorySize, 0, 0xffffffff}, vk::MemoryPropertyFlagBits::eHostVisible);
  EXPECT_EQ(2U * kMemorySize, allocator->GetTotalBytesAllocated());
  alloc2 = nullptr;
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());

  // Sub-allocations keep parent allocations alive.
  alloc = nullptr;
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  sub_alloc1 = nullptr;
  sub_alloc1a = nullptr;
  sub_alloc1b = nullptr;
  sub_alloc2 = nullptr;
  sub_alloc2a = nullptr;
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  sub_alloc2b = nullptr;
  EXPECT_EQ(0U, allocator->GetTotalBytesAllocated());
}

void TestAllocationOfBuffers(GpuAllocator* allocator) {
  const auto kBufferUsageFlags = vk::BufferUsageFlagBits::eTransferSrc |
                                 vk::BufferUsageFlagBits::eTransferDst;
  const auto kMemoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible |
                                    vk::MemoryPropertyFlagBits::eHostCoherent;

  // Allocate some buffers, and confirm that the allocator is tracking the bytes
  // allocated.
  auto buffer0 = allocator->AllocateBuffer(
      nullptr, kMemorySize, kBufferUsageFlags, kMemoryPropertyFlags, nullptr);
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  EXPECT_NE(nullptr, buffer0->host_ptr());
  EXPECT_EQ(kMemorySize, buffer0->size());
  auto buffer1 = allocator->AllocateBuffer(
      nullptr, kMemorySize, kBufferUsageFlags, kMemoryPropertyFlags, nullptr);
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());
  EXPECT_NE(nullptr, buffer1->host_ptr());
  EXPECT_EQ(kMemorySize, buffer1->size());

  // Allocate a buffer using dedicated memory and getting a separate managed
  // pointer to the memory.
  GpuMemPtr ptr;
  auto buffer_dedicated0 = allocator->AllocateBuffer(
      nullptr, kMemorySize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Release the objects, buffer first, and confirm that both need to be
  // destroyed before the memory is reclaimed.
  buffer_dedicated0 = nullptr;
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());
  ptr = nullptr;
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocate another dedicated memory object.
  buffer_dedicated0 = allocator->AllocateBuffer(
      nullptr, kMemorySize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Release the objects in the opposite order, and perform the same test.
  ptr = nullptr;
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());
  buffer_dedicated0 = nullptr;
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocate non-power-of-two buffers, proving that, even though the allocator
  // could use a small-buffer pool, the requirement of an output memory pointer
  // forces unique allocations (i.e., offset == 0) for all objects.
  const auto kSmallBufferSize = 5u;
  auto buffer_dedicated1 = allocator->AllocateBuffer(
      nullptr, kSmallBufferSize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated2 = allocator->AllocateBuffer(
      nullptr, kSmallBufferSize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated3 = allocator->AllocateBuffer(
      nullptr, kSmallBufferSize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated4 = allocator->AllocateBuffer(
      nullptr, kSmallBufferSize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated5 = allocator->AllocateBuffer(
      nullptr, kSmallBufferSize, kBufferUsageFlags, kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());

  // Release all objects.
  buffer0 = nullptr;
  buffer1 = nullptr;
  buffer_dedicated1 = nullptr;
  buffer_dedicated2 = nullptr;
  buffer_dedicated3 = nullptr;
  buffer_dedicated4 = nullptr;
  buffer_dedicated5 = nullptr;
  ptr = nullptr;

  // Confirm that all memory has been released.
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());
}

// The fake allocator is intended to be used when there is not a valid Vulkan
// instance. So this TEST should not need to be upgraded to a VK_TEST.
TEST(GpuMem, FakeAllocator) {
  FakeGpuAllocator allocator;

  TestAllocationOfMemory(&allocator);
  TestAllocationOfBuffers(&allocator);
}

// These tests check real Vulkan allocators, so they have a true dependency on
// Vulkan.
VK_TEST(GpuMem, NaiveAllocator) {
  VulkanInstance::Params instance_params(
      {{"VK_LAYER_LUNARG_standard_validation"},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME},
       false});

  auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
  auto vulkan_queues = VulkanDeviceQueues::New(vulkan_instance, {});
  NaiveGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfMemory(&allocator);

  // TODO(ES-173): This test crashes because we pass a null ResourceManager into
  // GpuAllocator. Creating a ResourceManager requires a functional Escher
  // object, but this test only needs a VulkanContext. This bug tracks
  // simplifying the dependency chain, so that all we need is a VulkanContext,
  // which we do have in this unit test.

  // TestAllocationOfBuffers(&allocator);
}

VK_TEST(GpuMem, VmaAllocator) {
  VulkanInstance::Params instance_params(
      {{"VK_LAYER_LUNARG_standard_validation"},
       {VK_EXT_DEBUG_REPORT_EXTENSION_NAME},
       false});

  auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
  // This extension is necessary for the VMA to support dedicated allocations.
  auto vulkan_queues = VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME},
       vk::SurfaceKHR(),
       VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent});
  VmaGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfMemory(&allocator);
  TestAllocationOfBuffers(&allocator);
}

}  // namespace
