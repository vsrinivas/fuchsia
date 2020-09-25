// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/ui/lib/escher/test/common/gtest_vulkan.h"
#include "src/ui/lib/escher/test/vk/fake_gpu_allocator.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/naive_gpu_allocator.h"
#include "src/ui/lib/escher/vk/vma_gpu_allocator.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"

namespace {
using namespace escher;
using namespace escher::test;
using ::testing::_;

// Comparison macro used for checking bytes_allocated.
#define EXPECT_BETWEEN_INCL(x, left, right) EXPECT_TRUE((x) >= (left) && (x) <= (right))

// Don't allow too much wasted memory.
//
// As VmaAllocator now treats memory heap less than 256MB as "small heaps"
// (defined in escher/BUILD.gn), and will allocate 1/64 of heap size for all
// small memory heaps, we set kMaxUnusedMemory = 4MB so that it will work
// correctly on all devices.
const vk::DeviceSize kMaxUnusedMemory = 4u * 1024 * 1024;

VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory) {
  VulkanInstance::Params instance_params({{}, {VK_EXT_DEBUG_REPORT_EXTENSION_NAME}, false});

  auto validation_layer_name = VulkanInstance::GetValidationLayerName();
  if (validation_layer_name) {
    instance_params.layer_names.insert(*validation_layer_name);
  }

  auto vulkan_instance = VulkanInstance::New(std::move(instance_params));
  // This test doesn't use the global Escher environment so TestWithVkValidationLayer won't work.
  // We set up a custom debug callback function to fail the test when there is errors / warnings /
  // performance warnings.
  vulkan_instance->RegisterDebugReportCallback(
      [](VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object,
         size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage,
         void* pUserData) -> VkBool32 {
        ADD_FAILURE() << "Debug report: " << vk::to_string(vk::DebugReportFlagsEXT(flags))
                      << " Object: " << object << " Location: " << location
                      << " Message code: " << messageCode << " Message: " << pMessage;
        return VK_FALSE;
      });

  VulkanDeviceQueues::Params::Flags flags =
      VulkanDeviceQueues::Params::kDisableQueueFilteringForPresent;
  if (use_protected_memory) {
    flags |= VulkanDeviceQueues::Params::kAllowProtectedMemory;
  }
  // This extension is necessary for the VMA to support dedicated allocations.
  auto vulkan_queues = VulkanDeviceQueues::New(
      vulkan_instance,
      {{VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME}, {}, vk::SurfaceKHR(), flags});
  // Some devices might not be capable of using protected memory.
  if (use_protected_memory && !vulkan_queues->caps().allow_protected_memory) {
    return nullptr;
  }
  return vulkan_queues;
}

struct AllocationStat {
  int64_t bytes_allocated;
  int64_t unused_bytes_allocated;
  size_t total_bytes_allocated;
  size_t total_unused_bytes_allocated;
};

void UpdateAllocationCount(std::vector<AllocationStat>& allocation_stat_each_test,
                           GpuAllocator* allocator) {
  size_t total_bytes_allocated = allocator->GetTotalBytesAllocated();
  size_t total_unused_bytes_allocated = allocator->GetUnusedBytesAllocated();

  int64_t bytes_allocated = static_cast<int64_t>(total_bytes_allocated);
  int64_t unused_bytes_allocated = static_cast<int64_t>(total_unused_bytes_allocated);
  if (allocation_stat_each_test.size() >= 1) {
    bytes_allocated -= allocation_stat_each_test.back().total_bytes_allocated;
    unused_bytes_allocated -= allocation_stat_each_test.back().total_unused_bytes_allocated;
  }

  allocation_stat_each_test.push_back(
      {.bytes_allocated = bytes_allocated,
       .unused_bytes_allocated = unused_bytes_allocated,
       .total_bytes_allocated = total_bytes_allocated,
       .total_unused_bytes_allocated = total_unused_bytes_allocated});
}

void TestAllocationOfMemory(GpuAllocator* allocator) {
  // vk_mem_alloc allocates power of 2 buffers by default, so this makes the
  // tests easier to verify.
  constexpr vk::DeviceSize kMemorySize = 1024;
  constexpr vk::DeviceSize kMemorySizeAllowableError = 64;

  std::vector<AllocationStat> allocation_stat_each_test;

  // Confirm that all memory has been released.
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[0]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_unused_bytes_allocated);

  // Standard sub-allocation tests.
  auto alloc = allocator->AllocateMemory({kMemorySize, 0, 0xffffffff},
                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[1]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Adding sub-allocations doesn't increase slab-count.
  auto sub_alloc1 = alloc->Suballocate(kMemorySize, 0);
  auto sub_alloc1a = sub_alloc1->Suballocate(kMemorySize, 0);
  auto sub_alloc1b = sub_alloc1->Suballocate(kMemorySize, 0);
  auto sub_alloc2 = alloc->Suballocate(kMemorySize, 0);
  auto sub_alloc2a = sub_alloc2->Suballocate(kMemorySize, 0);
  auto sub_alloc2b = sub_alloc2->Suballocate(kMemorySize, 0);

  // We expect that we didn't allocate any new memory.
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[2]
  EXPECT_EQ(0u, static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated));
  EXPECT_EQ(0u, static_cast<size_t>(allocation_stat_each_test.back().unused_bytes_allocated));

  // Allocating then freeing increases/decreases the slab-count.
  auto alloc2 = allocator->AllocateMemory({kMemorySize, 0, 0xffffffff},
                                          vk::MemoryPropertyFlagBits::eHostVisible);

  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[3]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  alloc2 = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[4]
  EXPECT_EQ(allocation_stat_each_test[2].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  // Sub-allocations keep parent allocations alive.
  alloc = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[5]
  EXPECT_EQ(allocation_stat_each_test[1].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  sub_alloc1 = nullptr;
  sub_alloc1a = nullptr;
  sub_alloc1b = nullptr;
  sub_alloc2 = nullptr;
  sub_alloc2a = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[6]
  EXPECT_EQ(allocation_stat_each_test[1].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  sub_alloc2b = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[7]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
}

void TestAllocationOfBuffers(GpuAllocator* allocator) {
  // vk_mem_alloc allocates power of 2 buffers by default, so this makes the
  // tests easier to verify.
  constexpr vk::DeviceSize kMemorySize = 1024;
  constexpr vk::DeviceSize kMemorySizeAllowableError = 64;

  std::vector<AllocationStat> allocation_stat_each_test;

  // Confirm that all memory has been released.
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[0]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_unused_bytes_allocated);

  const auto kBufferUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
  const auto kMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

  // Allocate some buffers, and confirm that the allocator is tracking the bytes
  // allocated.
  auto buffer0 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                           kMemoryPropertyFlags, nullptr);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[1]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  EXPECT_NE(nullptr, buffer0->host_ptr());
  EXPECT_EQ(kMemorySize, buffer0->size());

  auto buffer1 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                           kMemoryPropertyFlags, nullptr);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[2]
  EXPECT_LE(kMemorySize, static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated));

  EXPECT_NE(nullptr, buffer1->host_ptr());
  EXPECT_EQ(kMemorySize, buffer1->size());

  // Allocate a buffer using dedicated memory and getting a separate managed
  // pointer to the memory.
  GpuMemPtr ptr;
  auto buffer_dedicated0 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[3]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Release the objects, buffer first, and confirm that both need to be
  // destroyed before the memory is reclaimed.
  buffer_dedicated0 = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[4]
  EXPECT_EQ(allocation_stat_each_test[3].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  ptr = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[5]
  EXPECT_EQ(allocation_stat_each_test[2].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  // Allocate another dedicated memory object.
  buffer_dedicated0 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                                kMemoryPropertyFlags, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[6]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Release the objects in the opposite order, and perform the same test.
  ptr = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[7]
  EXPECT_EQ(allocation_stat_each_test[6].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  buffer_dedicated0 = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[8]
  EXPECT_EQ(allocation_stat_each_test[5].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  // Allocate non-power-of-two buffers, proving that, even though the allocator
  // could partition out a small pool, the requirement of an output memory
  // pointer forces unique allocations (i.e., offset == 0) for all objects.
  const auto kSmallBufferSize = 5u;
  auto buffer_dedicated1 = allocator->AllocateBuffer(nullptr, kSmallBufferSize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated2 = allocator->AllocateBuffer(nullptr, kSmallBufferSize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated3 = allocator->AllocateBuffer(nullptr, kSmallBufferSize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated4 = allocator->AllocateBuffer(nullptr, kSmallBufferSize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto buffer_dedicated5 = allocator->AllocateBuffer(nullptr, kSmallBufferSize, kBufferUsageFlags,
                                                     kMemoryPropertyFlags, &ptr);
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
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[9]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
}

void TestAllocationOfImages(GpuAllocator* allocator, bool use_protected_memory = false) {
  std::vector<AllocationStat> allocation_stat_each_test;

  // Confirm that all memory has been released.
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[0]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_unused_bytes_allocated);

  const auto kMemoryPropertyFlags = vk::MemoryPropertyFlagBits::eHostVisible;

  static const int kWidth = 64;
  static const int kHeight = 64;
  static const vk::Format kFormat = vk::Format::eR8G8B8A8Unorm;
  static const size_t kMemorySize = kWidth * kHeight * image_utils::BytesPerPixel(kFormat);
  static const vk::DeviceSize kMemorySizeAllowableError = 128;
  static const vk::ImageUsageFlags kUsage =
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

  ImageInfo info;
  info.format = kFormat;
  info.width = kWidth;
  info.height = kHeight;
  info.usage = kUsage;
  info.tiling = vk::ImageTiling::eLinear;
  info.memory_flags = kMemoryPropertyFlags;
  if (use_protected_memory) {
    info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }

  // Allocate some images, and confirm that the allocator is tracking the bytes
  // allocated.
  auto image0 = allocator->AllocateImage(nullptr, info);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[1]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Protected memory should not be accessible by the host.
  EXPECT_TRUE(use_protected_memory || image0->host_ptr() != nullptr);
  EXPECT_BETWEEN_INCL(image0->size(), kMemorySize, kMemorySize + kMemorySizeAllowableError);

  auto image1 = allocator->AllocateImage(nullptr, info);

  EXPECT_TRUE(use_protected_memory || image1->host_ptr() != nullptr);
  EXPECT_BETWEEN_INCL(image1->size(), kMemorySize, kMemorySize + kMemorySizeAllowableError);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[2]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Allocate an image using dedicated memory and getting a separate managed
  // pointer to the memory.
  GpuMemPtr ptr;
  auto image_dedicated0 = allocator->AllocateImage(nullptr, info, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_BETWEEN_INCL(ptr->size(), kMemorySize, kMemorySize + kMemorySizeAllowableError);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_TRUE(use_protected_memory || ptr->mapped_ptr() != nullptr);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[3]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_BETWEEN_INCL(static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated),
                      kMemorySize, kMemorySize + kMemorySizeAllowableError);

  // Release the objects, image first, and confirm that both need to be
  // destroyed before the memory is reclaimed.
  image_dedicated0 = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[4]
  EXPECT_EQ(allocation_stat_each_test[3].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  ptr = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[5]
  EXPECT_EQ(allocation_stat_each_test[2].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  // Allocate another dedicated memory object.
  image_dedicated0 = allocator->AllocateImage(nullptr, info, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_BETWEEN_INCL(ptr->size(), kMemorySize, kMemorySize + kMemorySizeAllowableError);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_TRUE(use_protected_memory || ptr->mapped_ptr() != nullptr);
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[6]

  // We ensure that for each allocation, the allocated byte size satisfies
  //   kMemorySize <= bytes_allocated <= kMemorySize + kMemorySizeAllowableError
  EXPECT_LE(kMemorySize, static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated));
  EXPECT_GE(kMemorySize + kMemorySizeAllowableError,
            static_cast<size_t>(allocation_stat_each_test.back().bytes_allocated));

  // Release the objects in the opposite order, and perform the same test.
  ptr = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[7]
  EXPECT_EQ(allocation_stat_each_test[6].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);
  image_dedicated0 = nullptr;
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[8]
  EXPECT_EQ(allocation_stat_each_test[5].total_bytes_allocated,
            allocation_stat_each_test.back().total_bytes_allocated);

  // Allocate non-power-of-two buffers, proving that, even though the allocator
  // could partition out a small pool, the requirement of an output memory
  // pointer forces unique allocations (i.e., offset == 0) for all objects.
  ImageInfo small_image;
  small_image.format = kFormat;
  small_image.width = 1;
  small_image.height = 1;
  small_image.usage = kUsage;
  small_image.tiling = vk::ImageTiling::eLinear;
  small_image.memory_flags = kMemoryPropertyFlags;

  auto image_dedicated1 = allocator->AllocateImage(nullptr, small_image, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto image_dedicated2 = allocator->AllocateImage(nullptr, small_image, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto image_dedicated3 = allocator->AllocateImage(nullptr, small_image, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto image_dedicated4 = allocator->AllocateImage(nullptr, small_image, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());
  auto image_dedicated5 = allocator->AllocateImage(nullptr, small_image, &ptr);
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_NE(nullptr, ptr->mapped_ptr());

  // Release all objects.
  image0 = nullptr;
  image1 = nullptr;
  image_dedicated1 = nullptr;
  image_dedicated2 = nullptr;
  image_dedicated3 = nullptr;
  image_dedicated4 = nullptr;
  image_dedicated5 = nullptr;
  ptr = nullptr;
  // Confirm that all memory has been released.
  UpdateAllocationCount(allocation_stat_each_test, allocator);  // Set allocation_stat_each_test[9]
  EXPECT_EQ(0u, allocation_stat_each_test.back().total_bytes_allocated);
}

// The fake allocator is intended to be used when there is not a valid Vulkan
// instance. So this TEST should not need to be upgraded to a VK_TEST.
TEST(FakeAllocator, Memory) {
  FakeGpuAllocator allocator;

  TestAllocationOfMemory(&allocator);
}

TEST(FakeAllocator, Buffers) {
  FakeGpuAllocator allocator;

  TestAllocationOfBuffers(&allocator);
}

TEST(FakeAllocator, Images) {
  FakeGpuAllocator allocator;

  TestAllocationOfImages(&allocator);
}

// These tests check real Vulkan allocators, so they have a true dependency on
// Vulkan.
VK_TEST(NaiveAllocator, NaiveAllocator) {
  auto vulkan_queues = CreateVulkanDeviceQueues(false);
  NaiveGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfMemory(&allocator);

  // TODO(fxbug.dev/7263): This test crashes because we pass a null ResourceManager into
  // GpuAllocator. Creating a ResourceManager requires a functional Escher
  // object, but this test only needs a VulkanContext. This bug tracks
  // simplifying the dependency chain, so that all we need is a VulkanContext,
  // which we do have in this unit test.

  // TestAllocationOfBuffers(&allocator);
  // TestAllocationOfImages(&allocator);
}

class VmaAllocator : public ::testing::TestWithParam</*protected_memory=*/bool> {};

VK_TEST_P(VmaAllocator, Memory) {
  auto vulkan_queues = CreateVulkanDeviceQueues(GetParam());
  if (!vulkan_queues) {
    GTEST_SKIP();
  }
  VmaGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfMemory(&allocator);
}

VK_TEST_P(VmaAllocator, Buffers) {
  auto vulkan_queues = CreateVulkanDeviceQueues(GetParam());
  if (!vulkan_queues) {
    GTEST_SKIP();
  }
  VmaGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfBuffers(&allocator);
}

VK_TEST_P(VmaAllocator, Images) {
  auto vulkan_queues = CreateVulkanDeviceQueues(GetParam());
  if (!vulkan_queues) {
    GTEST_SKIP();
  }
  VmaGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  TestAllocationOfImages(&allocator, GetParam());
}

INSTANTIATE_TEST_SUITE_P(VmaAllocatorTestSuite, VmaAllocator, ::testing::Bool());

class MockVmaGpuAllocator : public VmaGpuAllocator {
 public:
  MockVmaGpuAllocator(const VulkanContext& context) : VmaGpuAllocator(context) {}

  MOCK_METHOD5(CreateImage,
               bool(const VkImageCreateInfo& image_create_info,
                    const VmaAllocationCreateInfo& allocation_create_info, VkImage* image,
                    VmaAllocation* vma_allocation, VmaAllocationInfo* vma_allocation_info));
};

VK_TEST(VmaGpuAllocatorTest, ProtectedMemoryIsDedicated) {
  auto vulkan_queues = CreateVulkanDeviceQueues(/*use_protected_memory=*/true);
  if (!vulkan_queues) {
    GTEST_SKIP();
  }
  MockVmaGpuAllocator allocator(vulkan_queues->GetVulkanContext());

  VmaAllocationCreateInfo allocation_create_info;
  EXPECT_CALL(allocator, CreateImage(_, _, _, _, _))
      .Times(1)
      .WillOnce(DoAll(::testing::SaveArg<1>(&allocation_create_info), ::testing::Return(false)));
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.usage = vk::ImageUsageFlagBits::eTransferDst;
  info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  auto image0 = allocator.AllocateImage(nullptr, info, nullptr);
  EXPECT_TRUE(allocation_create_info.flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
}

}  // namespace
