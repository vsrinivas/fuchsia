// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/graphics/tests/vkreadback/vkreadback.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

#include "src/graphics/tests/common/utils.h"

#include <vulkan/vulkan.hpp>

#ifdef __Fuchsia__
#include <lib/zx/vmo.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#endif

namespace {

constexpr size_t kPageSize = 4096;

// Command buffers vary according the following dimensions:
// 1) includes an image transition
// 2) includes an image memory barrier
constexpr size_t kCommandBufferCount = 4;

// Note, alignment must be a power of 2
constexpr uint64_t round_up(uint64_t val, uint64_t alignment) {
  return ((val - 1) | (alignment - 1)) + 1;
}

}  // namespace

VkReadbackTest::VkReadbackTest(Extension ext)
    : ext_(ext),
      import_export_((ext == VK_FUCHSIA_EXTERNAL_MEMORY) ? EXPORT_EXTERNAL_MEMORY : SELF) {}

#ifdef __Fuchsia__
VkReadbackTest::VkReadbackTest(zx::vmo exported_memory_vmo)
    : ext_(VK_FUCHSIA_EXTERNAL_MEMORY),
      import_export_(IMPORT_EXTERNAL_MEMORY),
      exported_memory_vmo_(std::move(exported_memory_vmo)) {}
#endif  // __Fuchsia__

VkReadbackTest::~VkReadbackTest() = default;

bool VkReadbackTest::Initialize(uint32_t vk_api_verison) {
  if (is_initialized_) {
    return false;
  }

  if (!InitVulkan(vk_api_verison)) {
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
  auto [physical_devices_result, physical_devices] = instance->enumeratePhysicalDevices();
  if (vk::Result::eSuccess != physical_devices_result || physical_devices.empty()) {
    RTN_MSG(/* void */, "No physical device found: %s\n",
            vk::to_string(physical_devices_result).c_str());
  }

  for (const auto& phys_device : physical_devices) {
    vk::PhysicalDeviceProperties properties;
    phys_device.getProperties(&properties);

    if (VK_VERSION_MAJOR(properties.apiVersion) == 1 &&
        VK_VERSION_MINOR(properties.apiVersion) == 0) {
      printf("Skipping phys device that doesn't support Vulkan 1.1.\n");
      continue;
    }

    // Test external buffer/image capabilities.
    vk::PhysicalDeviceExternalBufferInfo buffer_info;
    buffer_info.usage = vk::BufferUsageFlagBits::eStorageBuffer;
    buffer_info.handleType = vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA;
    vk::ExternalBufferProperties buffer_properties =
        phys_device.getExternalBufferProperties(buffer_info);
    EXPECT_EQ(buffer_properties.externalMemoryProperties.externalMemoryFeatures,
              vk::ExternalMemoryFeatureFlagBits::eExportable |
                  vk::ExternalMemoryFeatureFlagBits::eImportable);
    EXPECT_TRUE(buffer_properties.externalMemoryProperties.exportFromImportedHandleTypes &
                vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
    EXPECT_TRUE(buffer_properties.externalMemoryProperties.compatibleHandleTypes &
                vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);

    vk::StructureChain<vk::PhysicalDeviceImageFormatInfo2,
                       vk::PhysicalDeviceExternalImageFormatInfo>
        image_format_info_chain;

    image_format_info_chain.get<vk::PhysicalDeviceImageFormatInfo2>()
        .setFormat(vk::Format::eR8G8B8A8Unorm)
        .setType(vk::ImageType::e2D)
        .setTiling(vk::ImageTiling::eLinear)
        .setUsage(vk::ImageUsageFlagBits::eTransferDst);

    image_format_info_chain.get<vk::PhysicalDeviceExternalImageFormatInfo>().handleType =
        vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA;

    auto [image_format_props_result, image_format_properties_chain] =
        phys_device.getImageFormatProperties2<vk::ImageFormatProperties2,
                                              vk::ExternalImageFormatProperties>(
            image_format_info_chain.get());
    EXPECT_EQ(image_format_props_result, vk::Result::eSuccess);

    vk::ExternalImageFormatProperties& external_format_properties =
        image_format_properties_chain.get<vk::ExternalImageFormatProperties>();
    EXPECT_EQ(external_format_properties.externalMemoryProperties.externalMemoryFeatures,
              vk::ExternalMemoryFeatureFlagBits::eExportable |
                  vk::ExternalMemoryFeatureFlagBits::eImportable);
    EXPECT_EQ(external_format_properties.externalMemoryProperties.exportFromImportedHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
    EXPECT_EQ(external_format_properties.externalMemoryProperties.compatibleHandleTypes,
              vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
  }
}
#endif  // __Fuchsia__

bool VkReadbackTest::InitVulkan(uint32_t vk_api_version) {
  if (vulkan_initialized_) {
    RTN_MSG(false, "InitVulkan failed.  Already initialized.\n");
  }
  std::vector<const char*> enabled_extension_names;
#ifdef __Fuchsia__
  if (import_export_ == IMPORT_EXTERNAL_MEMORY || import_export_ == EXPORT_EXTERNAL_MEMORY) {
    enabled_extension_names.push_back(VK_FUCHSIA_EXTERNAL_MEMORY_EXTENSION_NAME);
  }
#endif

  vk::ApplicationInfo app_info;
  app_info.pApplicationName = "vkreadback";
  app_info.apiVersion = vk_api_version;

  vk::InstanceCreateInfo instance_info;
  instance_info.pApplicationInfo = &app_info;

  // Copy the builder's default device info, which has its queue info
  // properly configured and modify the desired extension fields only.
  // Send the amended |device_info| back into the builder's
  // set_device_info() during unique context construction.
  VulkanContext::Builder builder;
  vk::DeviceCreateInfo device_info = builder.DeviceInfo();

  auto features = vk::PhysicalDeviceVulkan12Features().setTimelineSemaphore(true);
  auto ext_features = vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR().setTimelineSemaphore(true);

  timeline_semaphore_support_ = GetVulkanTimelineSemaphoreSupport(vk_api_version);
  switch (timeline_semaphore_support_) {
    case VulkanExtensionSupportState::kSupportedInCore:
      device_info.setPNext(&features);
      break;
    case VulkanExtensionSupportState::kSupportedAsExtensionOnly:
      enabled_extension_names.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
      device_info.setPNext(&ext_features);
      break;
    case VulkanExtensionSupportState::kNotSupported:
      break;
  }

  device_info.setPEnabledExtensionNames(enabled_extension_names);

  builder.set_instance_info(instance_info).set_device_info(device_info);
#ifdef __linux__
  // Validation layers not conveniently available yet in virtual Linux
  builder.set_validation_layers_enabled(false);
#endif
  ctx_ = builder.Unique();

#ifdef __Fuchsia__
  // Initialize Fuchsia external memory procs.
  if (import_export_ != SELF) {
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

  vk::PhysicalDeviceImageFormatInfo2 image_format_info;
  image_format_info.format = vk::Format::eR8G8B8A8Unorm;
  image_format_info.type = vk::ImageType::e2D;
  image_format_info.tiling = vk::ImageTiling::eLinear;
  image_format_info.usage = vk::ImageUsageFlagBits::eTransferDst;

  vk::ResultValue<vk::ImageFormatProperties2> get_image_format_properties_result =
      ctx_->physical_device().getImageFormatProperties2(image_format_info);
  RTN_IF_VKH_ERR(false, get_image_format_properties_result.result,
                 "vk::PhysicalDevice::getImageFormatProperties2()\n");

  vk::StructureChain<vk::ImageCreateInfo, vk::ExternalMemoryImageCreateInfo>
      image_create_info_chain;

  image_create_info_chain.get<vk::ImageCreateInfo>()
      .setFlags(vk::ImageCreateFlags())
      .setImageType(vk::ImageType::e2D)
      .setFormat(vk::Format::eR8G8B8A8Unorm)
      .setExtent(vk::Extent3D(kWidth, kHeight, 1))
      .setMipLevels(1)
      .setArrayLayers(1)
      .setSamples(vk::SampleCountFlagBits::e1)
      .setTiling(vk::ImageTiling::eLinear)
      .setUsage(vk::ImageUsageFlagBits::eTransferDst)
      .setSharingMode(vk::SharingMode::eExclusive)
      .setQueueFamilyIndices({})
      .setInitialLayout(vk::ImageLayout::eUndefined);

  if (import_export_ == SELF) {
    image_create_info_chain.unlink<vk::ExternalMemoryImageCreateInfo>();
  } else {
#ifdef __Fuchsia__
    image_create_info_chain.get<vk::ExternalMemoryImageCreateInfo>().handleTypes =
        vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA;
#endif
  }

  const auto& device = ctx_->device();
  vk::ResultValue<vk::UniqueImage> create_image_result =
      device->createImageUnique(image_create_info_chain.get());
  RTN_IF_VKH_ERR(false, create_image_result.result, "vk::Device::createImageUnique()\n");
  image_ = std::move(create_image_result.value);

  vk::StructureChain<vk::MemoryRequirements2, vk::MemoryDedicatedRequirements>
      memory_requirements_chain =
          device->getImageMemoryRequirements2<vk::MemoryRequirements2,
                                              vk::MemoryDedicatedRequirements>(
              vk::ImageMemoryRequirementsInfo2(*image_));
  const vk::MemoryRequirements& image_memory_requirements =
      memory_requirements_chain.get<vk::MemoryRequirements2>().memoryRequirements;
  use_dedicated_memory_ =
      memory_requirements_chain.get<vk::MemoryDedicatedRequirements>().requiresDedicatedAllocation;

  if (use_dedicated_memory_) {
    // If driver requires dedicated allocation to be used, per Vulkan specs,
    // the image offset can only be zero for dedicated allocation.
    bind_offset_ = 0u;
  } else {
    // Add an offset to all operations that's correctly aligned and at least a
    // page in size, to ensure rounding the VMO down to a page offset will
    // cause it to point to a separate page.
    constexpr uint32_t kOffset = 128;
    bind_offset_ = kPageSize + kOffset;
    if (image_memory_requirements.alignment) {
      bind_offset_ = round_up(bind_offset_, image_memory_requirements.alignment);
    }
  }

  vk::DeviceSize allocation_size = image_memory_requirements.size + bind_offset_;
  std::optional<uint32_t> memory_type =
      FindReadableMemoryType(allocation_size, image_memory_requirements.memoryTypeBits);
  if (!memory_type) {
    ADD_FAILURE()
        << "Memory requirements for linear images must always include a host-coherent memory type";
    return false;
  }

  vk::StructureChain<vk::MemoryAllocateInfo, vk::ExportMemoryAllocateInfoKHR,
                     vk::MemoryDedicatedAllocateInfo>
      mem_alloc_info_chain;
  mem_alloc_info_chain.get<vk::MemoryAllocateInfo>()
      .setAllocationSize(allocation_size)
      .setMemoryTypeIndex(memory_type.value());

  if (import_export_ == SELF) {
    mem_alloc_info_chain.unlink<vk::ExportMemoryAllocateInfoKHR>();
  }
#ifdef __Fuchsia__
  mem_alloc_info_chain.get<vk::ExportMemoryAllocateInfoKHR>().handleTypes =
      vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA;
#endif

  mem_alloc_info_chain.get<vk::MemoryDedicatedAllocateInfo>().image = *image_;
  if (!use_dedicated_memory_) {
    mem_alloc_info_chain.unlink<vk::MemoryDedicatedAllocateInfo>();
  }

  vk::ResultValue<vk::UniqueDeviceMemory> allocate_memory_result =
      device->allocateMemoryUnique(mem_alloc_info_chain.get());
  RTN_IF_VKH_ERR(false, allocate_memory_result.result, "vk::Device::allocateMemory()\n");
  device_memory_ = std::move(allocate_memory_result.value);

#ifdef __Fuchsia__
  if (import_export_ == IMPORT_EXTERNAL_MEMORY) {
    if (!AllocateFuchsiaImportedMemory(std::move(exported_memory_vmo_))) {
      RTN_MSG(false, "AllocateFuchsiaImportedMemory failed.\n");
    }
  } else if (import_export_ == EXPORT_EXTERNAL_MEMORY) {
    if (!AssignExportedMemoryHandle()) {
      RTN_MSG(false, "AssignExportedMemoryHandle failed.\n");
    }
  }
#endif  // __Fuchsia__

  auto [map_device_memory_result, device_memory_address] =
      device->mapMemory(*device_memory_, 0 /* offset= */, VK_WHOLE_SIZE, vk::MemoryMapFlags{});
  RTN_IF_VKH_ERR(false, map_device_memory_result, "vk::Device::mapMemory()\n");

  constexpr int kFill = 0xab;
  memset(device_memory_address, kFill, image_memory_requirements.size + bind_offset_);

  device->unmapMemory(*device_memory_);

  vk::Result bind_image_result = device->bindImageMemory(*image_, *device_memory_, bind_offset_);
  RTN_IF_VKH_ERR(false, bind_image_result, "vk::Device::bindImageMemory()\n");

  image_initialized_ = true;

  return true;
}

std::optional<uint32_t> VkReadbackTest::FindReadableMemoryType(vk::DeviceSize allocation_size,
                                                               uint32_t memory_type_bits) {
  vk::PhysicalDeviceMemoryProperties device_memory_properties =
      ctx_->physical_device().getMemoryProperties();
  EXPECT_LE(device_memory_properties.memoryTypeCount, VK_MAX_MEMORY_TYPES);

  uint32_t memory_type = 0;
  for (; memory_type < device_memory_properties.memoryTypeCount; ++memory_type) {
    if (!(memory_type_bits & (1 << memory_type)))
      continue;

    vk::MemoryType& memory_type_info = device_memory_properties.memoryTypes[memory_type];
    EXPECT_LE(memory_type_info.heapIndex, VK_MAX_MEMORY_HEAPS);
    if (device_memory_properties.memoryHeaps[memory_type_info.heapIndex].size < allocation_size)
      continue;

    vk::MemoryPropertyFlags memory_type_properties = memory_type_info.propertyFlags;

    // Restrict ourselves to host-coherent memory so we don't need to use
    // vkInvalidateMappedMemoryRanges() after mapping memory in Readback().
    if (!(memory_type_properties & vk::MemoryPropertyFlagBits::eHostCoherent))
      continue;

    EXPECT_TRUE(memory_type_properties & vk::MemoryPropertyFlagBits::eHostVisible)
        << "Host-coherent memory must always be host-visible";

    return memory_type;
  }
  return std::nullopt;
}

#ifdef __Fuchsia__
zx::vmo VkReadbackTest::TakeExportedMemoryVmo() { return std::move(exported_memory_vmo_); }

bool VkReadbackTest::AllocateFuchsiaImportedMemory(zx::vmo exported_memory_vmo) {
  size_t vmo_size = 0;
  {
    zx_status_t vmo_get_size_result = exported_memory_vmo.get_size(&vmo_size);
    if (vmo_get_size_result != ZX_OK) {
      RTN_MSG(false, "zx_vmo_get_size() failed with status: %d\n", vmo_get_size_result);
    }
  }

  const auto& device = ctx_->device();

  auto [zircon_handle_properties_result, zircon_handle_properties] =
      device->getMemoryZirconHandlePropertiesFUCHSIA(
          vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA, exported_memory_vmo.get(),
          ctx_->loader());
  RTN_IF_VKH_ERR(false, zircon_handle_properties_result,
                 "vkGetMemoryZirconHandlePropertiesFUCHSIA failed.\n");

  std::optional<uint32_t> memory_type =
      FindReadableMemoryType(vmo_size, zircon_handle_properties.memoryTypeBits);
  if (!memory_type) {
    RTN_MSG(false, "Can't find host mappable memory type for zircon VMO.\n");
  }

  vk::StructureChain<vk::MemoryAllocateInfo, vk::ImportMemoryZirconHandleInfoFUCHSIA>
      imported_mem_alloc_info_chain;

  vk::MemoryAllocateInfo& imported_mem_alloc_info =
      imported_mem_alloc_info_chain.get<vk::MemoryAllocateInfo>();
  imported_mem_alloc_info.allocationSize = vmo_size;
  imported_mem_alloc_info.memoryTypeIndex = memory_type.value();

  vk::ImportMemoryZirconHandleInfoFUCHSIA& import_memory_handle_info =
      imported_mem_alloc_info_chain.get<vk::ImportMemoryZirconHandleInfoFUCHSIA>();
  import_memory_handle_info.handleType = vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA;
  import_memory_handle_info.handle = exported_memory_vmo.get();

  vk::ResultValue<vk::UniqueDeviceMemory> allocate_memory_result =
      device->allocateMemoryUnique(imported_mem_alloc_info);
  RTN_IF_VKH_ERR(false, allocate_memory_result.result,
                 "vk::Device::allocateMemory() failed for import memory\n");

  imported_device_memory_ = std::move(allocate_memory_result.value);
  // After vk::Device::allocateMemory() succeeds, Vulkan owns the VMO handle.
  std::ignore = exported_memory_vmo.release();

  return true;
}

bool VkReadbackTest::AssignExportedMemoryHandle() {
  const auto& device = ctx_->device();

  vk::MemoryGetZirconHandleInfoFUCHSIA get_handle_info(
      *device_memory_, vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
  vk::ResultValue<zx_handle_t> get_memory_handle_result =
      device->getMemoryZirconHandleFUCHSIA(get_handle_info, ctx_->loader());
  RTN_IF_VKH_ERR(false, get_memory_handle_result.result, "vkGetMemoryZirconHandleFUCHSIA.\n");

  // vulkan.hpp doesn't have a *Unique() variant of
  // vk::Device::getMemoryZirconHandleFUCHSIA(), so we wrap the result in a scoped
  // handle type here.
  zx_handle_t exported_memory_vmo_handle = get_memory_handle_result.value;
  exported_memory_vmo_.reset(exported_memory_vmo_handle);

  vk::ResultValue<vk::MemoryZirconHandlePropertiesFUCHSIA> get_handle_properties_result =
      device->getMemoryZirconHandlePropertiesFUCHSIA(
          vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA, exported_memory_vmo_.get(),
          ctx_->loader());
  RTN_IF_VKH_ERR(false, get_handle_properties_result.result,
                 "vkGetMemoryZirconHandlePropertiesFUCHSIA\n");

  return true;
}
#endif  // __Fuchsia__

bool VkReadbackTest::FillCommandBuffer(VkReadbackSubmitOptions options,
                                       vk::UniqueCommandBuffer command_buffer) {
  auto begin_result = command_buffer->begin(vk::CommandBufferBeginInfo{});
  RTN_IF_VKH_ERR(false, begin_result, "vk::CommandBuffer::begin()\n");

  if (options.include_start_transition) {
    // Transition image for clear operation.
    vk::ImageMemoryBarrier image_barrier;
    image_barrier.image = image_.get();
    image_barrier.oldLayout = vk::ImageLayout::eUndefined;
    image_barrier.newLayout = vk::ImageLayout::eGeneral;
    image_barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    image_barrier.subresourceRange.levelCount = 1;
    image_barrier.subresourceRange.layerCount = 1;
    command_buffer->pipelineBarrier(/* srcStageMask= */ vk::PipelineStageFlagBits::eTopOfPipe,
                                    /* dstStageMask= */ vk::PipelineStageFlagBits::eTransfer,
                                    vk::DependencyFlags{}, /* memoryBarriers= */ {},
                                    /* bufferMemoryBarriers= */ {}, image_barrier);
  }

  // RGBA
  vk::ClearColorValue clear_color(std::array<float, 4>{1.0f, 0.0f, 0.5f, 0.75f});

  vk::ImageSubresourceRange image_subresource_range;
  image_subresource_range.aspectMask = vk::ImageAspectFlagBits::eColor;
  image_subresource_range.baseMipLevel = 0;
  image_subresource_range.levelCount = 1;
  image_subresource_range.baseArrayLayer = 0;
  image_subresource_range.layerCount = 1;

  command_buffer->clearColorImage(image_.get(), vk::ImageLayout::eGeneral, clear_color,
                                  image_subresource_range);

  if (options.include_end_barrier) {
    vk::ImageMemoryBarrier transfer_results_to_host_barrier;
    transfer_results_to_host_barrier.image = image_.get();
    transfer_results_to_host_barrier.subresourceRange = image_subresource_range;
    transfer_results_to_host_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    transfer_results_to_host_barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;

    // Equal queue family indexes mean no queue transfer occurs. The indexes
    // themselves are ignored.
    transfer_results_to_host_barrier.srcQueueFamilyIndex = 0;
    transfer_results_to_host_barrier.dstQueueFamilyIndex = 0;
    // Equal layouts means no layout transition occurs. The layout values are
    // ignored.
    transfer_results_to_host_barrier.oldLayout = vk::ImageLayout::eGeneral;
    transfer_results_to_host_barrier.newLayout = vk::ImageLayout::eGeneral;

    command_buffer->pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                                    vk::PipelineStageFlagBits::eHost, vk::DependencyFlags{},
                                    /*memoryBarriers=*/{}, /*bufferMemoryBarriers=*/{},
                                    {transfer_results_to_host_barrier});
  }

  vk::Result command_buffer_end_result = command_buffer->end();
  RTN_IF_VKH_ERR(false, command_buffer_end_result, "vk::UniqueCommandBuffer::end()\n");

  command_buffers_.try_emplace(options, std::move(command_buffer));
  return true;
}

bool VkReadbackTest::InitCommandBuffers() {
  if (command_buffers_initialized_) {
    RTN_MSG(false, "ERROR: Command buffers are already initialized.\n");
  }

  const auto& device = ctx_->device();
  vk::CommandPoolCreateInfo command_pool_create_info;
  command_pool_create_info.queueFamilyIndex = ctx_->queue_family_index();
  vk::ResultValue<vk::UniqueCommandPool> command_pool_result =
      device->createCommandPoolUnique(command_pool_create_info);
  RTN_IF_VKH_ERR(false, command_pool_result.result, "vk::Device::createCommandPoolUnique()\n");
  command_pool_ = std::move(command_pool_result.value);

  vk::CommandBufferAllocateInfo command_buffer_alloc_info;
  command_buffer_alloc_info.commandPool = *command_pool_;
  command_buffer_alloc_info.level = vk::CommandBufferLevel::ePrimary;
  command_buffer_alloc_info.commandBufferCount = kCommandBufferCount;
  vk::ResultValue<std::vector<vk::UniqueCommandBuffer>> command_buffer_result =
      device->allocateCommandBuffersUnique(command_buffer_alloc_info);
  RTN_IF_VKH_ERR(false, command_buffer_result.result,
                 "vk::Device::allocateCommandBuffersUnique()\n");

  bool fill_succeeded = true;
  fill_succeeded = fill_succeeded && FillCommandBuffer({.include_start_transition = false,
                                                        .include_end_barrier = false},
                                                       std::move(command_buffer_result.value[0]));
  fill_succeeded = fill_succeeded && FillCommandBuffer({.include_start_transition = false,
                                                        .include_end_barrier = true},
                                                       std::move(command_buffer_result.value[1]));
  fill_succeeded = fill_succeeded && FillCommandBuffer({.include_start_transition = true,
                                                        .include_end_barrier = false},
                                                       std::move(command_buffer_result.value[2]));
  fill_succeeded = fill_succeeded && FillCommandBuffer({.include_start_transition = true,
                                                        .include_end_barrier = true},
                                                       std::move(command_buffer_result.value[3]));

  command_buffers_initialized_ = true;

  return fill_succeeded;
}

bool VkReadbackTest::Exec(vk::Fence fence) {
  if (!Submit({.include_start_transition = true, .include_end_barrier = true}, fence)) {
    return false;
  }
  return Wait();
}

void VkReadbackTest::ValidateSubmitOptions(VkReadbackSubmitOptions options) {
  if (options.include_start_transition) {
    EXPECT_FALSE(submit_called_with_transition_)
        << "Submit() called with unnecessary include_start_transition option";
    submit_called_with_transition_ = true;
  } else {
    EXPECT_TRUE(submit_called_with_transition_)
        << "First Submit() called without include_start_transition option";
  }

  submit_called_with_barrier_ = options.include_end_barrier;
}

bool VkReadbackTest::Submit(VkReadbackSubmitOptions options, vk::Fence fence) {
  ValidateSubmitOptions(options);
  vk::CommandBuffer& command_buffer = command_buffers_[options].get();

  vk::SubmitInfo submit_info;
  submit_info.setCommandBuffers(command_buffer);
  vk::Result submit_result = ctx_->queue().submit(submit_info, fence);
  RTN_IF_VKH_ERR(false, submit_result, "vk::Queue::submit()\n");

  return true;
}

bool VkReadbackTest::Submit(VkReadbackSubmitOptions options, vk::Semaphore semaphore,
                            uint64_t signal) {
  ValidateSubmitOptions(options);
  vk::CommandBuffer& command_buffer = command_buffers_[options].get();

  vk::StructureChain<vk::SubmitInfo, vk::TimelineSemaphoreSubmitInfo> submit_info_chain;
  submit_info_chain.get<vk::SubmitInfo>().setSignalSemaphores(semaphore).setCommandBuffers(
      command_buffer);
  submit_info_chain.get<vk::TimelineSemaphoreSubmitInfo>().setSignalSemaphoreValues(signal);

  vk::Result submit_result = ctx_->queue().submit(submit_info_chain.get(), vk::Fence{});
  RTN_IF_VKH_ERR(false, submit_result, "vk::Queue::submit()\n");

  return true;
}

bool VkReadbackTest::Wait() {
  auto wait_idle_result = ctx_->queue().waitIdle();
  RTN_IF_VKH_ERR(false, wait_idle_result, "vk::Queue::waitIdle()\n");

  return true;
}

void VkReadbackTest::TransferSubmittedStateFrom(const VkReadbackTest& export_source) {
  EXPECT_EQ(IMPORT_EXTERNAL_MEMORY, import_export_)
      << __func__ << " called on VkReadbackTest without imported memory";
  EXPECT_EQ(EXPORT_EXTERNAL_MEMORY, export_source.import_export_)
      << __func__ << " called with VkReadbackTest test without exported memory";

  submit_called_with_transition_ = export_source.submit_called_with_transition_;
  submit_called_with_barrier_ = export_source.submit_called_with_barrier_;
}

bool VkReadbackTest::Readback() {
  EXPECT_TRUE(submit_called_with_barrier_)
      << "Readback() called after Submit() without include_end_barrier option";

  vk::DeviceMemory device_memory =
      ext_ == VkReadbackTest::NONE ? *device_memory_ : *imported_device_memory_;

  auto [map_result, map_address] = ctx_->device()->mapMemory(
      device_memory, /* offset= */ vk::DeviceSize{}, VK_WHOLE_SIZE, vk::MemoryMapFlags{});
  RTN_IF_VKH_ERR(false, map_result, "vk::Device::mapMemory()\n");

  auto* data = reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(map_address) + bind_offset_);

  // ABGR ordering of clear color value.
  const uint32_t kExpectedClearColorValue = 0xBF8000FF;

  int mismatches = 0;
  static_assert(kWidth < std::numeric_limits<int>::max() / kHeight,
                "kWidth * kHeight doesn't fit in an int");
  for (int i = 0; i < kWidth * kHeight; i++) {
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
