// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
#define SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/vmo.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/renderer/semaphore.h"

namespace escher {

// Create a new escher::Semaphore and a corresponding zx::event using
// the VK_KHR_EXTERNAL_SEMAPHORE_FD extension.  If it fails, both elements
// of the pair will be null.
std::pair<escher::SemaphorePtr, zx::event> NewSemaphoreEventPair(escher::Escher* escher);

// Exports a Semaphore into an event.
zx::event GetEventForSemaphore(VulkanDeviceQueues* device, const escher::SemaphorePtr& semaphore);

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

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_FUCHSIA_UTILS_H_
