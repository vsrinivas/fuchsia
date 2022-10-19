// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/fuchsia_utils.h"

#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"

#include "vulkan/vulkan_enums.hpp"
#include "vulkan/vulkan_structs.hpp"

namespace escher {

std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(escher::Escher* escher) {
  zx::event event;
  zx_status_t status = zx::event::create(0u, &event);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create event to import as VkSemaphore.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  zx::event event_copy;
  if (event.duplicate(ZX_RIGHT_SAME_RIGHTS, &event_copy) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to duplicate event.";
    return std::make_pair(escher::SemaphorePtr(), zx::event());
  }

  auto device = escher->device();
  auto sema = GetSemaphoreForEvent(device, std::move(event_copy));

  return std::make_pair(std::move(sema), std::move(event));
}

zx::event GetEventForSemaphore(VulkanDeviceQueues* device, const escher::SemaphorePtr& semaphore) {
  vk::SemaphoreGetZirconHandleInfoFUCHSIA info(
      semaphore->vk_semaphore(), vk::ExternalSemaphoreHandleTypeFlagBits::eZirconEventFUCHSIA);

  auto result =
      device->vk_device().getSemaphoreZirconHandleFUCHSIA(info, device->dispatch_loader());

  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(WARNING) << "unable to export semaphore";
    return zx::event();
  }
  return zx::event(result.value);
}

escher::SemaphorePtr GetSemaphoreForEvent(VulkanDeviceQueues* device, zx::event event) {
  auto sema = escher::Semaphore::New(device->vk_device());

  vk::ImportSemaphoreZirconHandleInfoFUCHSIA info;
  info.semaphore = sema->vk_semaphore();
  info.zirconHandle = event.release();
  info.handleType = vk::ExternalSemaphoreHandleTypeFlagBits::eZirconEventFUCHSIA;

  if (vk::Result::eSuccess !=
      device->vk_device().importSemaphoreZirconHandleFUCHSIA(info, device->dispatch_loader())) {
    FX_LOGS(ERROR) << "Failed to import event as VkSemaphore.";
    // Don't leak handle.
    zx_handle_close(info.zirconHandle);
    return escher::SemaphorePtr();
  }
  return sema;
}

zx::vmo ExportMemoryAsVmo(escher::Escher* escher, const escher::GpuMemPtr& mem) {
  vk::MemoryGetZirconHandleInfoFUCHSIA export_memory_info(
      mem->base(), vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
  auto result = escher->vk_device().getMemoryZirconHandleFUCHSIA(
      export_memory_info, escher->device()->dispatch_loader());
  if (result.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Failed to export escher::GpuMem as zx::vmo";
    return zx::vmo();
  }
  return zx::vmo(result.value);
}

std::pair<escher::GpuMemPtr, escher::ImagePtr> GenerateExportableMemImage(
    vk::Device device, escher::ResourceManager* resource_manager,
    const escher::ImageInfo& image_info) {
  FX_DCHECK(device);
  FX_DCHECK(resource_manager);
  FX_DCHECK(image_info.is_external);

  // Create vk::Image
  constexpr auto kInitialLayout = vk::ImageLayout::ePreinitialized;
  auto create_info = escher::image_utils::CreateVkImageCreateInfo(image_info, kInitialLayout);
  vk::Image vk_image = device.createImage(create_info).value;

  vk::MemoryRequirements reqs = device.getImageMemoryRequirements(vk_image);
  uint32_t memory_type = 0;
  for (; memory_type < 32; memory_type++) {
    if ((reqs.memoryTypeBits & (1U << memory_type))) {
      break;
    }
  }

  // Allocate vk::Memory.
  vk::MemoryDedicatedAllocateInfo dedicated_allocate_info(vk_image, vk::Buffer());
  vk::ExportMemoryAllocateInfoKHR export_allocate_info(
      vk::ExternalMemoryHandleTypeFlagBits::eZirconVmoFUCHSIA);
  export_allocate_info.setPNext(&dedicated_allocate_info);

  vk::MemoryAllocateInfo alloc_info(reqs.size, memory_type);
  alloc_info.setPNext(&export_allocate_info);
  auto vk_memory = device.allocateMemory(alloc_info).value;

  // Convert vk::DeviceMemory to escher::GpuMem, vk::Image to escher::Image
  auto mem =
      escher::GpuMem::AdoptVkMemory(device, vk_memory, reqs.size, false /* needs_mapped_ptr */);
  auto image = escher::impl::NaiveImage::AdoptVkImage(resource_manager, image_info, vk_image, mem,
                                                      kInitialLayout);
  return std::make_pair(mem, image);
}

vk::Format SysmemPixelFormatTypeToVkFormat(fuchsia::sysmem::PixelFormatType pixel_format) {
  switch (pixel_format) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
      return vk::Format::eB8G8R8A8Srgb;
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      return vk::Format::eR8G8B8A8Srgb;
    case fuchsia::sysmem::PixelFormatType::NV12:
      return vk::Format::eG8B8R82Plane420Unorm;
    case fuchsia::sysmem::PixelFormatType::I420:
      return vk::Format::eG8B8R83Plane420Unorm;
    default:
      break;
  }
  return vk::Format::eUndefined;
}

namespace {

// Image formats supported by Escher / Scenic in a priority order.
const vk::Format kPreferredImageFormats[] = {vk::Format::eR8G8B8A8Srgb, vk::Format::eB8G8R8A8Srgb,
                                             vk::Format::eG8B8R83Plane420Unorm,
                                             vk::Format::eG8B8R82Plane420Unorm};

// Color spaces supported by Escher / Scenic.
// TODO(fxbug.dev/91778): Currently Escher converts all YUV images to RGBA using
// BT709 color conversion matrix; however, the images may use other color
// spaces, which will cause inaccurate color presentation when using non-BT709
// images. Escher should support sampling such images as well.
const vk::SysmemColorSpaceFUCHSIA kPreferredRgbColorSpace = {
    static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::SRGB)};
const vk::SysmemColorSpaceFUCHSIA kPreferredYuvColorSpaces[] = {
    {static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC709)},
    {static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_NTSC)},
    {static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE)},
    {static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_PAL)},
    {static_cast<uint32_t>(fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE)},
};

}  // namespace

vk::ImageFormatConstraintsInfoFUCHSIA GetDefaultImageFormatConstraintsInfo(
    const vk::ImageCreateInfo& create_info) {
  FX_DCHECK(create_info.format != vk::Format::eUndefined);
  FX_DCHECK(create_info.usage != vk::ImageUsageFlags{});
  bool is_yuv = image_utils::IsYuvFormat(create_info.format);
  vk::ImageFormatConstraintsInfoFUCHSIA format_info;
  format_info.setImageCreateInfo(create_info)
      .setRequiredFormatFeatures(image_utils::GetFormatFeatureFlagsFromUsage(create_info.usage))
      .setSysmemPixelFormat({})
      .setColorSpaceCount(
          is_yuv ? sizeof(kPreferredYuvColorSpaces) / sizeof(kPreferredYuvColorSpaces[0]) : 1u)
      .setPColorSpaces(is_yuv ? kPreferredYuvColorSpaces : &kPreferredRgbColorSpace);
  return format_info;
}

ImageConstraintsInfo GetDefaultImageConstraintsInfo(const vk::ImageCreateInfo& create_info,
                                                    bool allow_protected_memory) {
  ImageConstraintsInfo result;

  if (create_info.format != vk::Format::eUndefined) {
    result.format_constraints.push_back(GetDefaultImageFormatConstraintsInfo(create_info));
  } else {
    for (const auto format : kPreferredImageFormats) {
      auto new_create_info = create_info;
      new_create_info.setFormat(format);
      result.format_constraints.push_back(GetDefaultImageFormatConstraintsInfo(new_create_info));
    }
  }

  result.image_constraints
      .setFlags(allow_protected_memory ? vk::ImageConstraintsInfoFlagBitsFUCHSIA::eProtectedOptional
                                       : vk::ImageConstraintsInfoFlagsFUCHSIA{})
      .setFormatConstraints(result.format_constraints)
      .setBufferCollectionConstraints(
          vk::BufferCollectionConstraintsInfoFUCHSIA().setMinBufferCount(1u));

  // The compiler may do named return value optimization here; otherwise it will
  // invoke the move ctor of |ImageConstraintsInfo|.
  return result;
}

// Converts sysmem ColorSpace enum to Escher ColorSpace enum.
ColorSpace FromSysmemColorSpace(fuchsia::sysmem::ColorSpaceType sysmem_color_space) {
  switch (sysmem_color_space) {
    case fuchsia::sysmem::ColorSpaceType::INVALID:
      return ColorSpace::kInvalid;
    case fuchsia::sysmem::ColorSpaceType::SRGB:
      return ColorSpace::kSrgb;
    case fuchsia::sysmem::ColorSpaceType::REC601_NTSC:
      return ColorSpace::kRec601Ntsc;
    case fuchsia::sysmem::ColorSpaceType::REC601_NTSC_FULL_RANGE:
      return ColorSpace::kRec601NtscFullRange;
    case fuchsia::sysmem::ColorSpaceType::REC601_PAL:
      return ColorSpace::kRec601Pal;
    case fuchsia::sysmem::ColorSpaceType::REC601_PAL_FULL_RANGE:
      return ColorSpace::kRec601PalFullRange;
    case fuchsia::sysmem::ColorSpaceType::REC709:
      return ColorSpace::kRec709;
    case fuchsia::sysmem::ColorSpaceType::REC2020:
      return ColorSpace::kRec2020;
    case fuchsia::sysmem::ColorSpaceType::REC2100:
      return ColorSpace::kRec2100;
    case fuchsia::sysmem::ColorSpaceType::PASS_THROUGH:
      return ColorSpace::kPassThrough;
    case fuchsia::sysmem::ColorSpaceType::DO_NOT_CARE:
      return ColorSpace::kDoNotCare;
  }
}

}  // namespace escher
