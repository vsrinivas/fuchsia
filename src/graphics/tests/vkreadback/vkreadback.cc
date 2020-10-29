// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "vkreadback.h"

#include <array>

#include "src/graphics/tests/common/utils.h"
#include "vulkan/vulkan.h"
#include "vulkan/vulkan_core.h"

#include "vulkan/vulkan.hpp"

namespace {

constexpr size_t kPageSize = 4096;

}  // namespace

#ifdef __Fuchsia__
#include <zircon/syscalls.h>
#endif

// Note, alignment must be a power of 2
template <class T>
static inline T round_up(T val, uint32_t alignment) {
  return ((val - 1) | (alignment - 1)) + 1;
}

VkReadbackTest::VkReadbackTest(Extension ext)
    : ext_(ext),
      import_export_((ext == VK_FUCHSIA_EXTERNAL_MEMORY) ? EXPORT_EXTERNAL_MEMORY : SELF),
      command_buffers_(kNumCommandBuffers) {}

VkReadbackTest::VkReadbackTest(uint32_t exported_memory_handle)
    : ext_(VK_FUCHSIA_EXTERNAL_MEMORY),
      exported_memory_handle_(exported_memory_handle),
      import_export_(IMPORT_EXTERNAL_MEMORY),
      command_buffers_(kNumCommandBuffers) {}

VkReadbackTest::~VkReadbackTest() {
  if (image_initialized_) {
    if (VK_NULL_HANDLE != device_memory_) {
      ctx_->device()->freeMemory(device_memory_, nullptr /* allocator */);
    }
    if (VK_NULL_HANDLE != imported_device_memory_) {
      ctx_->device()->freeMemory(imported_device_memory_, nullptr /* allocator */);
    }
  }
}

bool VkReadbackTest::Initialize() {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan()) {
    RTN_MSG(false, "Failed to initialize Vulkan\n");
  }

  if (!InitImage()) {
    RTN_MSG(false, "InitImage failed\n");
  }

  if (!InitCommandBuffers()) {
    RTN_MSG(false, "InitCommandBuffers failed\n");
  }

  is_initialized_ = command_buffers_initialized_ && image_initialized_ && vulkan_initialized_;

  return true;
}

#ifdef __Fuchsia__
void VkReadbackTest::VerifyExpectedImageFormats() const {
  const auto& instance = ctx_->instance();
  auto [rv_physical_devices, physical_devices] = instance->enumeratePhysicalDevices();
  if (vk::Result::eSuccess != rv_physical_devices || physical_devices.empty()) {
    RTN_MSG(/* void */, "No physical device found: 0x%0x", rv_physical_devices);
  }

  for (const auto& phys_device : physical_devices) {
    vk::PhysicalDeviceProperties properties;
    phys_device.getProperties(&properties);

    if (VK_VERSION_MAJOR(properties.apiVersion) == 1 &&
        VK_VERSION_MINOR(properties.apiVersion) == 0) {
      printf("Skipping phys device that doesn't support Vulkan 1.1.\n");
      continue;
    }

    // Test external buffer/image capabilities
    vk::PhysicalDeviceExternalBufferInfo buffer_info;
    buffer_info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    buffer_info.handleType = vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;
    vk::ExternalBufferProperties buffer_props;
    phys_device.getExternalBufferProperties(&buffer_info, &buffer_props);
    EXPECT_EQ(buffer_props.externalMemoryProperties.externalMemoryFeatures,
              vk::ExternalMemoryFeatureFlagBits::eExportable |
                  vk::ExternalMemoryFeatureFlagBits::eImportable);
    EXPECT_EQ(buffer_props.externalMemoryProperties.exportFromImportedHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
    EXPECT_EQ(buffer_props.externalMemoryProperties.compatibleHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);

    vk::PhysicalDeviceExternalImageFormatInfo ext_image_format_info;
    ext_image_format_info.handleType = vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;
    vk::PhysicalDeviceImageFormatInfo2 image_format_info;
    image_format_info.pNext = &ext_image_format_info;
    image_format_info.format = vk::Format::eR8G8B8A8Unorm;
    image_format_info.type = vk::ImageType::e2D;
    image_format_info.tiling = vk::ImageTiling::eLinear;
    image_format_info.usage = vk::ImageUsageFlagBits::eTransferDst;

    vk::ExternalImageFormatProperties ext_format_props;

    vk::ImageFormatProperties2 image_format_props2;
    image_format_props2.pNext = &ext_format_props;

    auto rv_image_format_props =
        phys_device.getImageFormatProperties2(&image_format_info, &image_format_props2);
    EXPECT_EQ(rv_image_format_props, vk::Result::eSuccess);
    EXPECT_EQ(ext_format_props.externalMemoryProperties.externalMemoryFeatures,
              vk::ExternalMemoryFeatureFlagBits::eExportable |
                  vk::ExternalMemoryFeatureFlagBits::eImportable);
    EXPECT_EQ(ext_format_props.externalMemoryProperties.exportFromImportedHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
    EXPECT_EQ(ext_format_props.externalMemoryProperties.compatibleHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA);
  }
}
#endif  // __Fuchsia__

bool VkReadbackTest::InitVulkan() {
  if (vulkan_initialized_) {
    RTN_MSG(false, "InitVulkan failed.  Already initialized.\n")
  }
  std::vector<const char*> enabled_extension_names;
#ifdef __Fuchsia__
  if (import_export_ == IMPORT_EXTERNAL_MEMORY || import_export_ == EXPORT_EXTERNAL_MEMORY) {
    enabled_extension_names.push_back(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
  }
#endif

  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkreadback";
  app_info.apiVersion = VK_API_VERSION_1_1;

  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  // Copy the builder's default device info, which has its queue info
  // properly configured and modify the desired extension fields only.
  // Send the amended |device_info| back into the builder's
  // set_device_info() during unique context construction.
  VulkanContext::Builder builder;
  vk::DeviceCreateInfo device_info = builder.DeviceInfo();
  device_info.enabledExtensionCount = enabled_extension_names.size();
  device_info.ppEnabledExtensionNames = enabled_extension_names.data();

  ctx_ = builder.set_instance_info(instance_info).set_device_info(device_info).Unique();

#ifdef __Fuchsia__
  // Initialize Fuchsia external memory procs.
  if (import_export_ != SELF) {
    vkGetMemoryZirconHandleFUCHSIA_ = reinterpret_cast<PFN_vkGetMemoryZirconHandleFUCHSIA>(
        ctx_->instance()->getProcAddr("vkGetMemoryZirconHandleFUCHSIA"));
    if (!vkGetMemoryZirconHandleFUCHSIA_) {
      RTN_MSG(false, "Couldn't find vkGetMemoryZirconHandleFUCHSIA\n");
    }

    vkGetMemoryZirconHandlePropertiesFUCHSIA_ =
        reinterpret_cast<PFN_vkGetMemoryZirconHandlePropertiesFUCHSIA>(
            ctx_->instance()->getProcAddr("vkGetMemoryZirconHandlePropertiesFUCHSIA"));
    if (!vkGetMemoryZirconHandlePropertiesFUCHSIA_) {
      RTN_MSG(false, "Couldn't find vkGetMemoryZirconHandlePropertiesFUCHSIA\n");
    }
    VerifyExpectedImageFormats();
  }
#endif

  vulkan_initialized_ = true;

  return true;
}

bool VkReadbackTest::InitImage() {
  if (image_initialized_) {
    RTN_MSG(false, "Image already initialized.\n");
  }

  vk::ImageCreateInfo image_create_info;
  image_create_info.flags = vk::ImageCreateFlagBits::eMutableFormat;
  image_create_info.imageType = vk::ImageType::e2D;
  image_create_info.format = vk::Format::eR8G8B8A8Unorm;
  image_create_info.extent = vk::Extent3D(kWidth, kHeight, 1);
  image_create_info.mipLevels = 1;
  image_create_info.arrayLayers = 1;
  image_create_info.samples = vk::SampleCountFlagBits::e1;
  image_create_info.tiling = vk::ImageTiling::eLinear;
  image_create_info.usage = vk::ImageUsageFlagBits::eTransferDst;
  image_create_info.sharingMode = vk::SharingMode::eExclusive;
  image_create_info.queueFamilyIndexCount = 0;
  image_create_info.initialLayout = vk::ImageLayout::ePreinitialized;

#ifdef __Fuchsia__
  vk::ExternalMemoryImageCreateInfo external_memory_create_info;
  if (import_export_ != SELF) {
    external_memory_create_info.handleTypes =
        vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;
    image_create_info.pNext = &external_memory_create_info;
  }
#endif

  vk::PhysicalDeviceImageFormatInfo2 image_format_info;
  image_format_info.format = vk::Format::eR8G8B8A8Unorm;
  image_format_info.type = vk::ImageType::e2D;
  image_format_info.tiling = vk::ImageTiling::eLinear;
  image_format_info.usage = vk::ImageUsageFlagBits::eTransferDst;

  const auto& phys_device = ctx_->physical_device();

  vk::ImageFormatProperties2 image_format_properties2;
  auto rv_get_image_props =
      phys_device.getImageFormatProperties2(&image_format_info, &image_format_properties2);
  RTN_IF_VKH_ERR(false, rv_get_image_props, "vk::PhysicalDevice::getImageFormatProperties2()\n");

  const auto& device = ctx_->device();
  auto [rv_image, image] = device->createImageUnique(image_create_info);
  RTN_IF_VKH_ERR(false, rv_image, "vk::Device::createImageUnique()\n");
  image_ = std::move(image);

  vk::MemoryRequirements memory_reqs;
  device->getImageMemoryRequirements(image_.get(), &memory_reqs);

  // Add an offset to all operations that's correctly aligned and at least a
  // page in size, to ensure rounding the VMO down to a page offset will
  // cause it to point to a separate page.
  constexpr uint32_t kOffset = 128;
  bind_offset_ = kPageSize + kOffset;
  if (memory_reqs.alignment) {
    bind_offset_ = round_up(bind_offset_, memory_reqs.alignment);
  }

  vk::PhysicalDeviceMemoryProperties memory_props;
  ctx_->physical_device().getMemoryProperties(&memory_props);

  uint32_t memory_type = 0;
  for (; memory_type < VK_MAX_MEMORY_TYPES; memory_type++) {
    if ((memory_reqs.memoryTypeBits & (1 << memory_type)) &&
        (memory_props.memoryTypes[memory_type].propertyFlags &
         vk::MemoryPropertyFlagBits::eHostVisible)) {
      break;
    }
  }
  if (memory_type >= VK_MAX_MEMORY_TYPES) {
    RTN_MSG(false, "Can't find host mappable memory type for image.\n");
  }

  vk::ExportMemoryAllocateInfoKHR export_info;
  export_info.handleTypes = vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;

  vk::MemoryAllocateInfo mem_alloc_info;
  mem_alloc_info.pNext = (import_export_ == IMPORT_EXTERNAL_MEMORY) ? &export_info : nullptr;
  mem_alloc_info.allocationSize = memory_reqs.size + bind_offset_;
  mem_alloc_info.memoryTypeIndex = memory_type;

  auto rv_device_memory =
      device->allocateMemory(&mem_alloc_info, nullptr /* allocator */, &device_memory_);
  RTN_IF_VKH_ERR(false, rv_device_memory, "vk::Device::allocateMemory()\n");

#ifdef __Fuchsia__
  if (import_export_ == IMPORT_EXTERNAL_MEMORY) {
    if (!AllocateFuchsiaImportedMemory(exported_memory_handle_)) {
      RTN_MSG(false, "AllocateFuchsiaImportedMemory failed.\n");
    }
  } else if (import_export_ == EXPORT_EXTERNAL_MEMORY) {
    if (!AssignExportedMemoryHandle()) {
      RTN_MSG(false, "AssignExportedMemoryHandle failed.\n");
    }
  }
#endif  // __Fuchsia__

  void* addr;
  auto rv_map_mem =
      device->mapMemory(device_memory_, 0 /* offset */, VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &addr);
  RTN_IF_VKH_ERR(false, rv_map_mem, "vk::Device::mapMemory()\n");

  constexpr int kFill = 0xab;
  memset(addr, kFill, memory_reqs.size + bind_offset_);

  device->unmapMemory(device_memory_);

  auto rv_bind = device->bindImageMemory(image_.get(), device_memory_, bind_offset_);
  RTN_IF_VKH_ERR(false, rv_bind, "vk::Device::bindImageMemory()\n");

  image_initialized_ = true;

  return true;
}

#ifdef __Fuchsia__
bool VkReadbackTest::AllocateFuchsiaImportedMemory(uint32_t exported_memory_handle) {
  const auto& device = ctx_->device();

  if (exported_memory_handle == 0u) {
    RTN_MSG(false, "|exported_memory_handle| must be initialized.\n");
  }

  size_t vmo_size;
  zx_vmo_get_size(exported_memory_handle, &vmo_size);

  VkMemoryZirconHandlePropertiesFUCHSIA zircon_handle_props{
      .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA,
      .pNext = nullptr,
  };
  VkResult result = vkGetMemoryZirconHandlePropertiesFUCHSIA_(
      *device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA, exported_memory_handle,
      &zircon_handle_props);
  RTN_IF_VK_ERR(false, result, "vkGetMemoryZirconHandlePropertiesFUCHSIA failed.\n");

  // Find index of lowest set bit.
  uint32_t memory_type = __builtin_ctz(zircon_handle_props.memoryTypeBits);

  vk::ImportMemoryZirconHandleInfoFUCHSIA import_memory_handle_info;
  import_memory_handle_info.pNext = nullptr;
  import_memory_handle_info.handleType =
      vk::ExternalMemoryHandleTypeFlagBits::eTempZirconVmoFUCHSIA;
  import_memory_handle_info.handle = exported_memory_handle;

  vk::MemoryAllocateInfo imported_mem_alloc_info;
  imported_mem_alloc_info.pNext = &import_memory_handle_info;
  imported_mem_alloc_info.allocationSize = vmo_size;
  imported_mem_alloc_info.memoryTypeIndex = memory_type;

  auto rv_imported_device_memory = device->allocateMemory(
      &imported_mem_alloc_info, nullptr /* allocator */, &imported_device_memory_);
  RTN_IF_VKH_ERR(false, rv_imported_device_memory,
                 "vk::Device::allocateMemory() failed for import memory\n");

  return true;
}

bool VkReadbackTest::AssignExportedMemoryHandle() {
  const auto& device = ctx_->device();
  VkMemoryGetZirconHandleInfoFUCHSIA get_handle_info = {
      .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_GET_ZIRCON_HANDLE_INFO_FUCHSIA,
      .pNext = nullptr,
      .memory = device_memory_,
      .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA};
  VkResult result =
      vkGetMemoryZirconHandleFUCHSIA_(*device, &get_handle_info, &exported_memory_handle_);
  RTN_IF_VK_ERR(false, result, "vkGetMemoryZirconHandleFUCHSIA.\n");

  VkMemoryZirconHandlePropertiesFUCHSIA zircon_handle_props{
      .sType = VK_STRUCTURE_TYPE_TEMP_MEMORY_ZIRCON_HANDLE_PROPERTIES_FUCHSIA,
      .pNext = nullptr,
  };
  result = vkGetMemoryZirconHandlePropertiesFUCHSIA_(
      *device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA, exported_memory_handle_,
      &zircon_handle_props);
  RTN_IF_VK_ERR(false, result, "vkGetMemoryZirconHandlePropertiesFUCHSIA\n");

  return true;
}
#endif  // __Fuchsia__

bool VkReadbackTest::FillCommandBuffer(vk::CommandBuffer& command_buffer, bool transition_image) {
  auto rv_begin = command_buffer.begin(vk::CommandBufferBeginInfo{});
  RTN_IF_VKH_ERR(false, rv_begin, "vk::CommandBuffer::begin()\n");

  if (transition_image) {
    // Transition image for clear operation.
    vk::ImageMemoryBarrier image_barrier;
    image_barrier.image = image_.get();
    image_barrier.oldLayout = vk::ImageLayout::ePreinitialized;
    image_barrier.newLayout = vk::ImageLayout::eGeneral;
    image_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.layerCount = 1;
    command_buffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, /* srcStageMask */
                                   vk::PipelineStageFlagBits::eTransfer,  /* dstStageMask */
                                   vk::DependencyFlags{}, 0 /* memoryBarrierCount */,
                                   nullptr /* pMemoryBarriers */, 0 /* bufferMemoryBarrierCount */,
                                   nullptr /* pBufferMemoryBarriers */,
                                   1 /* imageMemoryBarrierCount */, &image_barrier);
  }

  // RGBA
  vk::ClearColorValue clear_color(std::array<float, 4>{1.0f, 0.0f, 0.5f, 0.75f});

  vk::ImageSubresourceRange image_subres_range;
  image_subres_range.aspectMask = vk::ImageAspectFlagBits::eColor;
  image_subres_range.baseMipLevel = 0;
  image_subres_range.levelCount = 1;
  image_subres_range.baseArrayLayer = 0;
  image_subres_range.layerCount = 1;

  command_buffer.clearColorImage(image_.get(), vk::ImageLayout::eGeneral, &clear_color,
                                 1 /* rangeCount */, &image_subres_range);
  auto rv_command_buf_end = command_buffer.end();
  RTN_IF_VKH_ERR(false, rv_command_buf_end, "vk::UniqueCommandBuffer::end()\n");

  return true;
}

bool VkReadbackTest::InitCommandBuffers() {
  if (command_buffers_initialized_) {
    RTN_MSG(false, "ERROR: Command buffers are already initialized.\n");
  }

  const auto& device = ctx_->device();
  vk::CommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.queueFamilyIndex = ctx_->queue_family_index();
  auto [rv_command_pool, command_pool] = device->createCommandPoolUnique(command_pool_create_info);
  RTN_IF_VKH_ERR(false, rv_command_pool, "vk::Device::createCommandPoolUnique()\n");
  command_pool_ = std::move(command_pool);

  vk::CommandBufferAllocateInfo command_buffer_alloc_info;
  command_buffer_alloc_info.commandPool = command_pool_.get();
  command_buffer_alloc_info.level = vk::CommandBufferLevel::ePrimary;
  command_buffer_alloc_info.commandBufferCount = kNumCommandBuffers;
  auto [rv_alloc_cmd_bufs, command_buffers] =
      device->allocateCommandBuffersUnique(command_buffer_alloc_info);
  RTN_IF_VKH_ERR(false, rv_alloc_cmd_bufs, "vk::Device::allocateCommandBuffersUnique()\n");
  command_buffers_ = std::move(command_buffers);
  if (!FillCommandBuffer(command_buffers_[0].get(), true /* transition_image */))
    return false;
  if (!FillCommandBuffer(command_buffers_[1].get(), false /* transition_image */))
    return false;

  command_buffers_initialized_ = true;

  return true;
}

bool VkReadbackTest::Exec(vk::Fence fence) {
  if (!Submit(fence, true /* transition_image */)) {
    return false;
  }
  return Wait();
}

bool VkReadbackTest::Submit(vk::Fence fence, bool transition_image) {
  vk::SubmitInfo submit_info;
  submit_info.commandBufferCount = 1;
  const vk::CommandBuffer& command_buffer =
      (transition_image ? command_buffers_[0].get() : command_buffers_[1].get());
  submit_info.pCommandBuffers = &command_buffer;
  auto rv_submit = ctx_->queue().submit(1, &submit_info, fence);
  RTN_IF_VKH_ERR(false, rv_submit, "vk::Queue::submit()\n");

  return true;
}

bool VkReadbackTest::Wait() {
  auto rv_idle = ctx_->queue().waitIdle();
  RTN_IF_VKH_ERR(false, rv_idle, "vk::Queue::waitIdle()\n");

  return true;
}

bool VkReadbackTest::Readback() {
  void* addr;
  const vk::DeviceMemory& device_memory =
      ext_ == VkReadbackTest::NONE ? device_memory_ : imported_device_memory_;

  auto rv_map = ctx_->device()->mapMemory(device_memory, vk::DeviceSize{} /* offset */,
                                          VK_WHOLE_SIZE, vk::MemoryMapFlags{}, &addr);
  RTN_IF_VKH_ERR(false, rv_map, "vk::Device::mapMemory()\n");

  auto* data = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(addr) + bind_offset_);

  // ABGR ordering of clear color value.
  const uint32_t kExpectedClearColorValue = 0xBF8000FF;

  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < kWidth * kHeight; i++) {
    if (data[i] != kExpectedClearColorValue) {
      constexpr int kMaxMismatches = 10;
      if (mismatches++ < kMaxMismatches) {
        fprintf(stderr, "Clear Color Value Mismatch at index %d - expected 0x%08x, got 0x%08x\n", i,
                kExpectedClearColorValue, data[i]);
      }
    }
  }
  if (mismatches) {
    fprintf(stdout, "****** Test Failed! %d mismatches\n", mismatches);
  }

  ctx_->device()->unmapMemory(device_memory);

  return mismatches == 0;
}
