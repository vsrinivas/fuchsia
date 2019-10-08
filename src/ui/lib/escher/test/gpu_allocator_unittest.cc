#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/ui/lib/escher/test/fake_gpu_allocator.h"
#include "src/ui/lib/escher/test/gtest_vulkan.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/naive_gpu_allocator.h"
#include "src/ui/lib/escher/vk/vma_gpu_allocator.h"
#include "src/ui/lib/escher/vk/vulkan_context.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"

namespace {
using namespace escher;
using namespace escher::test;
using ::testing::_;

VulkanDeviceQueuesPtr CreateVulkanDeviceQueues(bool use_protected_memory) {
  VulkanInstance::Params instance_params(
      {{"VK_LAYER_KHRONOS_validation"}, {VK_EXT_DEBUG_REPORT_EXTENSION_NAME}, false});

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

// vk_mem_alloc allocates power of 2 buffers by default, so this makes the
// tests easier to verify.
const vk::DeviceSize kMemorySize = 1024;

void TestAllocationOfMemory(GpuAllocator* allocator) {
  // Confirm that all memory has been released.
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());

  // Standard sub-allocation tests.
  auto alloc = allocator->AllocateMemory({kMemorySize, 0, 0xffffffff},
                                         vk::MemoryPropertyFlagBits::eDeviceLocal);

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
  auto alloc2 = allocator->AllocateMemory({kMemorySize, 0, 0xffffffff},
                                          vk::MemoryPropertyFlagBits::eHostVisible);
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
  // Confirm that all memory has been released.
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());

  const auto kBufferUsageFlags =
      vk::BufferUsageFlagBits::eTransferSrc | vk::BufferUsageFlagBits::eTransferDst;
  const auto kMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

  // Allocate some buffers, and confirm that the allocator is tracking the bytes
  // allocated.
  auto buffer0 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                           kMemoryPropertyFlags, nullptr);
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  EXPECT_NE(nullptr, buffer0->host_ptr());
  EXPECT_EQ(kMemorySize, buffer0->size());
  auto buffer1 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                           kMemoryPropertyFlags, nullptr);
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());
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
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Release the objects, buffer first, and confirm that both need to be
  // destroyed before the memory is reclaimed.
  buffer_dedicated0 = nullptr;
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());
  ptr = nullptr;
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocate another dedicated memory object.
  buffer_dedicated0 = allocator->AllocateBuffer(nullptr, kMemorySize, kBufferUsageFlags,
                                                kMemoryPropertyFlags, &ptr);
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
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());
}

void TestAllocationOfImages(GpuAllocator* allocator, bool use_protected_memory = false) {
  // Confirm that all memory has been released.
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());

  const auto kMemoryPropertyFlags =
      vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;

  static const int kWidth = 64;
  static const int kHeight = 64;
  static const vk::Format kFormat = vk::Format::eR8G8B8A8Unorm;
  static const size_t kMemorySize = kWidth * kHeight * image_utils::BytesPerPixel(kFormat);
  static const vk::ImageUsageFlags kUsage =
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst;

  ImageInfo info;
  info.format = kFormat;
  info.width = kWidth;
  info.height = kHeight;
  info.usage = kUsage;
  info.tiling = vk::ImageTiling::eLinear;
  if (use_protected_memory) {
    info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }

  // Allocate some images, and confirm that the allocator is tracking the bytes
  // allocated.
  auto image0 = allocator->AllocateImage(nullptr, info);
  EXPECT_EQ(kMemorySize, allocator->GetTotalBytesAllocated());
  // Protected memory should not be accessible by the host.
  EXPECT_TRUE(use_protected_memory || image0->host_ptr() != nullptr);
  EXPECT_EQ(kMemorySize, image0->size());
  auto image1 = allocator->AllocateImage(nullptr, info);
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());
  EXPECT_TRUE(use_protected_memory || image1->host_ptr() != nullptr);
  EXPECT_EQ(kMemorySize, image1->size());

  // Allocate an image using dedicated memory and getting a separate managed
  // pointer to the memory.
  GpuMemPtr ptr;
  auto image_dedicated0 = allocator->AllocateImage(nullptr, info, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_TRUE(use_protected_memory || ptr->mapped_ptr() != nullptr);
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Release the objects, image first, and confirm that both need to be
  // destroyed before the memory is reclaimed.
  image_dedicated0 = nullptr;
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());
  ptr = nullptr;
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocate another dedicated memory object.
  image_dedicated0 = allocator->AllocateImage(nullptr, info, &ptr);
  EXPECT_TRUE(ptr);
  EXPECT_EQ(kMemorySize, ptr->size());
  EXPECT_EQ(0u, ptr->offset());
  EXPECT_TRUE(use_protected_memory || ptr->mapped_ptr() != nullptr);
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Release the objects in the opposite order, and perform the same test.
  ptr = nullptr;
  EXPECT_EQ(3 * kMemorySize, allocator->GetTotalBytesAllocated());
  image_dedicated0 = nullptr;
  EXPECT_EQ(2 * kMemorySize, allocator->GetTotalBytesAllocated());

  // Allocate non-power-of-two buffers, proving that, even though the allocator
  // could partition out a small pool, the requirement of an output memory
  // pointer forces unique allocations (i.e., offset == 0) for all objects.
  ImageInfo small_image;
  small_image.format = kFormat;
  small_image.width = 1;
  small_image.height = 1;
  small_image.usage = kUsage;
  info.tiling = vk::ImageTiling::eLinear;

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
  EXPECT_EQ(0u, allocator->GetTotalBytesAllocated());
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

  // TODO(ES-173): This test crashes because we pass a null ResourceManager into
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
  info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  auto image0 = allocator.AllocateImage(nullptr, info, nullptr);
  EXPECT_TRUE(allocation_create_info.flags & VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT);
}

}  // namespace
