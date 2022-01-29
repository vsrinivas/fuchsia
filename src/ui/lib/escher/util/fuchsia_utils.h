// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
#define SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include "src/lib/fxl/macros.h"
#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/renderer/semaphore.h"
#include "src/ui/lib/escher/vk/color_space.h"

namespace escher {

// Create a new escher::Semaphore and a corresponding zx::event using
// the VK_KHR_EXTERNAL_SEMAPHORE_FD extension.  If it fails, both elements
// of the pair will be null.
std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(escher::Escher* escher);

// Exports a Semaphore into an event.
zx::event GetEventForSemaphore(VulkanDeviceQueues* device, const escher::SemaphorePtr& semaphore);

// Imports an event into a Semaphore.
escher::SemaphorePtr GetSemaphoreForEvent(VulkanDeviceQueues* device, zx::event event);

// Export the escher::GpuMem as a zx::vmo.
zx::vmo ExportMemoryAsVmo(escher::Escher* escher, const escher::GpuMemPtr& mem);

// Generate an escher Image and GPU memory dedicated to that image.
// The GPU memory will be exportable as a vmo object in Fuchsia by calling
// escher::ExportMemoryAsVmo function.
std::pair<escher::GpuMemPtr, escher::ImagePtr> GenerateExportableMemImage(
    vk::Device device, escher::ResourceManager* resource_manager,
    const escher::ImageInfo& image_info);

// Converts sysmem pixel format to equivalent vk::Format.
vk::Format SysmemPixelFormatTypeToVkFormat(fuchsia::sysmem::PixelFormatType pixel_format);

// Given a VkImageCreateInfo with a specific format, this returns the default
// VkImageFormatConstraintsInfoFUCHSIA which could be used in
// VkImageConstraintsInfoFUCHSIA to set Vulkan drivers sysmem constraints.
// - The |format| field of VkImageCreateInfo cannot be |eUndefined|, and
//   the |usage| field cannot be null.
// - It will only request format features based on |usage| field.
// - It will not request any extra sysmem pixel format.
// - It will use SRGB color space for images with SRGB formats, otherwise it
//   will use REC709 for YUV formats.
vk::ImageFormatConstraintsInfoFUCHSIA GetDefaultImageFormatConstraintsInfo(
    const vk::ImageCreateInfo& create_info);

struct ImageConstraintsInfo {
  ImageConstraintsInfo() = default;

  // Note that for functions returning an |ImageConstraintsInfo| object,
  // compilers may optimize the named return value, that case the returned
  // object will be initialized directly and this move constructor won't be
  // invoked. If compiler doesn't optimize the named return value, this move
  // ctor will be invoked at return time. But this move constructor must be
  // implemented no matter whether the compiler does the optimization.
  ImageConstraintsInfo(ImageConstraintsInfo&& from)
      : format_constraints(std::move(from.format_constraints)),
        image_constraints(std::move(from.image_constraints)) {
    image_constraints.setFormatConstraints(format_constraints);
  }
  FXL_DISALLOW_COPY_AND_ASSIGN(ImageConstraintsInfo);

  std::vector<vk::ImageFormatConstraintsInfoFUCHSIA> format_constraints;
  vk::ImageConstraintsInfoFUCHSIA image_constraints;
};

// Given a VkImageCreateInfo with or without a specific format, this returns the
// default VkImageConstraintsInfoFUCHSIA which could be used to set Vulkan
// drivers sysmem constraints.
// - If the |format| is not |eUndefined|, the generated constraints info will
//   only support that given format. Otherwise, the generated constraints info
//   will include support for all the Scenic-preferred RGBA and YUV formats.
// - All the other assumptions are the same as |GetDefaultImageFormatConstraintsInfo|.
ImageConstraintsInfo GetDefaultImageConstraintsInfo(const vk::ImageCreateInfo& create_info,
                                                    bool allow_protected_memory = false);

// Converts sysmem ColorSpace enum to Escher ColorSpace enum.
ColorSpace FromSysmemColorSpace(fuchsia::sysmem::ColorSpaceType sysmem_color_space);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
