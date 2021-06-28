// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <gbm.h>

#include <gtest/gtest.h>

#include "src/graphics/tests/common/vulkan_context.h"

#include <vulkan/vulkan.hpp>

class VkGbm : public testing::Test {
 public:
  void SetUp() override {
    fd_ = open("/dev/magma0", O_RDWR | O_CLOEXEC);
    ASSERT_GE(fd_, 0);

    device_ = gbm_create_device(fd_);
    ASSERT_TRUE(device_);

    const char *app_name = "vkgbm";
    auto app_info =
        vk::ApplicationInfo().setPApplicationName(app_name).setApiVersion(VK_API_VERSION_1_1);

    vk::InstanceCreateInfo instance_info;
    instance_info.pApplicationInfo = &app_info;

    std::array<const char *, 2> device_extensions{VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
                                                  VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME};

    auto builder = VulkanContext::Builder();
    builder.set_instance_info(instance_info).set_validation_layers_enabled(false);
    builder.set_device_info(builder.DeviceInfo().setPEnabledExtensionNames(device_extensions));

    context_ = builder.Unique();
    ASSERT_TRUE(context_);
  }

  void TearDown() override {
    gbm_device_destroy(device_);
    device_ = nullptr;

    close(fd_);
    fd_ = -1;
  }

  VulkanContext *context() { return context_.get(); }

  struct gbm_device *device() {
    return device_;
  }

  void IsMemoryTypeCoherent(uint32_t memoryTypeIndex, bool *is_coherent_out);
  void WriteLinearImage(vk::DeviceMemory memory, bool is_coherent, uint64_t row_bytes,
                        uint32_t height, uint32_t fill);
  void CheckLinearImage(vk::DeviceMemory memory, bool is_coherent, uint64_t row_bytes,
                        uint32_t height, uint32_t fill);

 private:
  int fd_ = -1;
  struct gbm_device *device_ = nullptr;
  std::unique_ptr<VulkanContext> context_;
};

void VkGbm::IsMemoryTypeCoherent(uint32_t memoryTypeIndex, bool *is_coherent_out) {
  vk::PhysicalDeviceMemoryProperties props = context_->physical_device().getMemoryProperties();
  ASSERT_LT(memoryTypeIndex, props.memoryTypeCount);
  *is_coherent_out = static_cast<bool>(props.memoryTypes[memoryTypeIndex].propertyFlags &
                                       vk::MemoryPropertyFlagBits::eHostCoherent);
}

void VkGbm::WriteLinearImage(vk::DeviceMemory memory, bool is_coherent, uint64_t row_bytes,
                             uint32_t height, uint32_t fill) {
  void *addr;
  vk::Result result = context_->device()->mapMemory(memory, 0 /* offset */, VK_WHOLE_SIZE,
                                                    vk::MemoryMapFlags{}, &addr);
  ASSERT_EQ(vk::Result::eSuccess, result);

  for (uint32_t y = 0; y < height; y++) {
    auto row_addr = reinterpret_cast<uint8_t *>(addr) + y * row_bytes;
    for (uint32_t x = 0; x < row_bytes; x += sizeof(uint32_t)) {
      *reinterpret_cast<uint32_t *>(row_addr + x) = fill;
    }
  }

  if (!is_coherent) {
    auto range = vk::MappedMemoryRange().setMemory(memory).setSize(VK_WHOLE_SIZE);
    context_->device()->flushMappedMemoryRanges(1, &range);
  }

  context_->device()->unmapMemory(memory);
}

void VkGbm::CheckLinearImage(vk::DeviceMemory memory, bool is_coherent, uint64_t row_bytes,
                             uint32_t height, uint32_t fill) {
  void *addr;
  vk::Result result = context_->device()->mapMemory(memory, 0 /* offset */, VK_WHOLE_SIZE,
                                                    vk::MemoryMapFlags{}, &addr);
  ASSERT_EQ(vk::Result::eSuccess, result);

  if (!is_coherent) {
    auto range = vk::MappedMemoryRange().setMemory(memory).setSize(VK_WHOLE_SIZE);
    context_->device()->invalidateMappedMemoryRanges(1, &range);
  }

  for (uint32_t y = 0; y < height; y++) {
    auto row_addr = reinterpret_cast<uint8_t *>(addr) + y * row_bytes;
    for (uint32_t x = 0; x < row_bytes; x += sizeof(uint32_t)) {
      EXPECT_EQ(fill, *reinterpret_cast<uint32_t *>(row_addr + x))
          << "offset " << y * row_bytes + x;
    }
  }

  context_->device()->unmapMemory(memory);
}

constexpr uint32_t kDefaultWidth = 1920;
constexpr uint32_t kDefaultHeight = 1080;
constexpr uint32_t kDefaultGbmFormat = GBM_FORMAT_ARGB8888;
constexpr vk::Format kDefaultVkFormat = vk::Format::eB8G8R8A8Unorm;
constexpr uint32_t kPattern = 0xaabbccdd;

using UniqueGbmBo = std::unique_ptr<struct gbm_bo, decltype(&gbm_bo_destroy)>;

using UseLinearDst = bool;

class VkGbmWithParam : public VkGbm, public ::testing::WithParamInterface<UseLinearDst> {};

TEST_P(VkGbmWithParam, ImportImageCopy) {
  bool useLinearDst = GetParam();

  auto src_bo =
      UniqueGbmBo(gbm_bo_create(device(), kDefaultWidth, kDefaultHeight, kDefaultGbmFormat,
                                GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR),
                  gbm_bo_destroy);
  ASSERT_TRUE(src_bo);

  auto dst_bo =
      UniqueGbmBo(gbm_bo_create(device(), kDefaultWidth, kDefaultHeight, kDefaultGbmFormat,
                                GBM_BO_USE_RENDERING | (useLinearDst ? GBM_BO_USE_LINEAR : 0)),
                  gbm_bo_destroy);
  ASSERT_TRUE(dst_bo);

  vk::UniqueImage src_image;
  vk::UniqueDeviceMemory src_memory;
  uint64_t src_row_bytes;

  {
    std::array<uint64_t, 1> modifiers{gbm_bo_get_modifier(src_bo.get())};
    auto format_modifier_create_info =
        vk::ImageDrmFormatModifierListCreateInfoEXT().setDrmFormatModifiers(modifiers);

    auto external_create_info = vk::ExternalMemoryImageCreateInfo().setHandleTypes(
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);
    external_create_info.setPNext(&format_modifier_create_info);

    auto create_info = vk::ImageCreateInfo()
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(kDefaultVkFormat)
                           .setExtent(vk::Extent3D(kDefaultWidth, kDefaultHeight, 1))
                           .setMipLevels(1)
                           .setArrayLayers(1)
                           .setSamples(vk::SampleCountFlagBits::e1)
                           .setTiling(vk::ImageTiling::eDrmFormatModifierEXT)
                           .setUsage(vk::ImageUsageFlagBits::eTransferSrc)
                           .setSharingMode(vk::SharingMode::eExclusive)
                           .setInitialLayout(vk::ImageLayout::ePreinitialized)
                           .setPNext(&external_create_info);

    auto result = context()->device()->createImageUnique(create_info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    src_image = std::move(result.value);

    auto subresource = vk::ImageSubresource().setAspectMask(vk::ImageAspectFlagBits::ePlane0KHR);
    auto layout = context()->device()->getImageSubresourceLayout(src_image.get(), subresource);
    ASSERT_EQ(layout.offset, 0u);
    src_row_bytes = layout.rowPitch;
    EXPECT_GE(src_row_bytes, kDefaultWidth * sizeof(uint32_t));
  }

  {
    auto memory_reqs_chain =
        context()
            ->device()
            ->getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                src_image.get());
    // Validate that image creation did not allocate a magma image
    EXPECT_FALSE(
        memory_reqs_chain.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation);

    auto &mem_reqs = memory_reqs_chain.get<vk::MemoryRequirements2>().memoryRequirements;
    uint32_t memory_type_index = __builtin_ctz(mem_reqs.memoryTypeBits);

    int fd = gbm_bo_get_fd(src_bo.get());
    EXPECT_GE(fd, 0);

    auto import_info = vk::ImportMemoryFdInfoKHR().setFd(fd).setHandleType(
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);

    auto alloc_info = vk::MemoryAllocateInfo()
                          .setAllocationSize(mem_reqs.size)
                          .setMemoryTypeIndex(memory_type_index)
                          .setPNext(&import_info);

    auto result = context()->device()->allocateMemoryUnique(alloc_info);
    ASSERT_EQ(result.result, vk::Result::eSuccess);
    src_memory = std::move(result.value);

    ASSERT_EQ(vk::Result::eSuccess,
              context()->device()->bindImageMemory(src_image.get(), src_memory.get(), 0u));

    bool is_coherent;
    IsMemoryTypeCoherent(memory_type_index, &is_coherent);

    WriteLinearImage(src_memory.get(), is_coherent, src_row_bytes, kDefaultHeight, kPattern);
  }

  vk::UniqueImage dst_image;
  vk::UniqueDeviceMemory dst_memory;
  bool dst_is_coherent;
  uint64_t dst_row_bytes;

  {
    std::array<uint64_t, 1> modifiers{gbm_bo_get_modifier(dst_bo.get())};
    auto format_modifier_create_info =
        vk::ImageDrmFormatModifierListCreateInfoEXT().setDrmFormatModifiers(modifiers);

    auto external_create_info = vk::ExternalMemoryImageCreateInfo().setHandleTypes(
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);
    external_create_info.setPNext(&format_modifier_create_info);

    auto create_info = vk::ImageCreateInfo()
                           .setImageType(vk::ImageType::e2D)
                           .setFormat(kDefaultVkFormat)
                           .setExtent(vk::Extent3D(kDefaultWidth, kDefaultHeight, 1))
                           .setMipLevels(1)
                           .setArrayLayers(1)
                           .setSamples(vk::SampleCountFlagBits::e1)
                           .setTiling(vk::ImageTiling::eDrmFormatModifierEXT)
                           .setUsage(vk::ImageUsageFlagBits::eTransferDst)
                           .setSharingMode(vk::SharingMode::eExclusive)
                           .setInitialLayout(vk::ImageLayout::eUndefined)
                           .setPNext(&external_create_info);

    auto result = context()->device()->createImageUnique(create_info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    dst_image = std::move(result.value);

    auto subresource = vk::ImageSubresource().setAspectMask(vk::ImageAspectFlagBits::ePlane0KHR);
    auto layout = context()->device()->getImageSubresourceLayout(dst_image.get(), subresource);
    ASSERT_EQ(layout.offset, 0u);
    dst_row_bytes = layout.rowPitch;
    EXPECT_GE(dst_row_bytes, kDefaultWidth * sizeof(uint32_t));
  }

  {
    auto memory_reqs_chain =
        context()
            ->device()
            ->getImageMemoryRequirements2<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>(
                dst_image.get());
    // Validate that image creation did not allocate a magma image
    EXPECT_FALSE(
        memory_reqs_chain.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation);

    auto &mem_reqs = memory_reqs_chain.get<vk::MemoryRequirements2>().memoryRequirements;
    uint32_t memory_type_index = __builtin_ctz(mem_reqs.memoryTypeBits);

    int fd = gbm_bo_get_fd(dst_bo.get());
    EXPECT_GE(fd, 0);

    auto import_info = vk::ImportMemoryFdInfoKHR().setFd(fd).setHandleType(
        vk::ExternalMemoryHandleTypeFlagBitsKHR::eOpaqueFd);

    auto alloc_info = vk::MemoryAllocateInfo()
                          .setAllocationSize(mem_reqs.size)
                          .setMemoryTypeIndex(memory_type_index)
                          .setPNext(&import_info);

    auto result = context()->device()->allocateMemoryUnique(alloc_info);
    ASSERT_EQ(result.result, vk::Result::eSuccess);
    dst_memory = std::move(result.value);

    ASSERT_EQ(vk::Result::eSuccess,
              context()->device()->bindImageMemory(dst_image.get(), dst_memory.get(), 0u));

    IsMemoryTypeCoherent(memory_type_index, &dst_is_coherent);

    if (useLinearDst)
      WriteLinearImage(dst_memory.get(), dst_is_coherent, dst_row_bytes, kDefaultHeight,
                       0xffffffff);
  }

  vk::UniqueCommandPool command_pool;
  {
    auto info = vk::CommandPoolCreateInfo().setQueueFamilyIndex(context()->queue_family_index());
    auto result = context()->device()->createCommandPoolUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_pool = std::move(result.value);
  }

  std::vector<vk::UniqueCommandBuffer> command_buffers;
  {
    auto info = vk::CommandBufferAllocateInfo()
                    .setCommandPool(command_pool.get())
                    .setLevel(vk::CommandBufferLevel::ePrimary)
                    .setCommandBufferCount(1);
    auto result = context()->device()->allocateCommandBuffersUnique(info);
    ASSERT_EQ(vk::Result::eSuccess, result.result);
    command_buffers = std::move(result.value);
  }

  {
    auto info = vk::CommandBufferBeginInfo();
    command_buffers[0]->begin(&info);
  }

  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(src_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferRead)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferSrcOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }
  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eHostWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setOldLayout(vk::ImageLayout::ePreinitialized)
                       .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eHost,     /* srcStageMask */
        vk::PipelineStageFlagBits::eTransfer, /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  {
    auto layer = vk::ImageSubresourceLayers()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLayerCount(1);
    auto copy = vk::ImageCopy()
                    .setSrcSubresource(layer)
                    .setDstSubresource(layer)
                    .setSrcOffset({0, 0, 0})
                    .setDstOffset({0, 0, 0})
                    .setExtent({kDefaultWidth, kDefaultHeight, 1});
    command_buffers[0]->copyImage(src_image.get(), vk::ImageLayout::eTransferSrcOptimal,
                                  dst_image.get(), vk::ImageLayout::eTransferDstOptimal, 1, &copy);
  }

  {
    auto range = vk::ImageSubresourceRange()
                     .setAspectMask(vk::ImageAspectFlagBits::eColor)
                     .setLevelCount(1)
                     .setLayerCount(1);
    auto barrier = vk::ImageMemoryBarrier()
                       .setImage(dst_image.get())
                       .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
                       .setDstAccessMask(vk::AccessFlagBits::eHostRead)
                       .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
                       .setNewLayout(vk::ImageLayout::eGeneral)
                       .setSubresourceRange(range);
    command_buffers[0]->pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer, /* srcStageMask */
        vk::PipelineStageFlagBits::eHost,     /* dstStageMask */
        vk::DependencyFlags{}, 0 /* memoryBarrierCount */, nullptr /* pMemoryBarriers */,
        0 /* bufferMemoryBarrierCount */, nullptr /* pBufferMemoryBarriers */,
        1 /* imageMemoryBarrierCount */, &barrier);
  }

  command_buffers[0]->end();

  {
    auto command_buffer_temp = command_buffers[0].get();
    auto info = vk::SubmitInfo().setCommandBufferCount(1).setPCommandBuffers(&command_buffer_temp);
    context()->queue().submit(1, &info, vk::Fence());
  }

  context()->queue().waitIdle();

  if (useLinearDst)
    CheckLinearImage(dst_memory.get(), dst_is_coherent, dst_row_bytes, kDefaultHeight, kPattern);
}

INSTANTIATE_TEST_SUITE_P(, VkGbmWithParam, ::testing::Bool());
