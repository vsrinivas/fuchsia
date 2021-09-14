// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_READBACK_H_
#define SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_READBACK_H_

#include <optional>

#include "src/graphics/examples/vkproto/common/utils.h"

namespace vkp {

//
// TransitionToHostVisibleImage
//
// Creates a host visible destination image and transitions |src_image| to it.
// |src_image| layout must be vk::ImageLayout::eTransferSrcOptimal prior to
// calling this function.  Transition command submission is synchronous.
//
// Returns the device memory of the newly created host image and populates
// |host_image_layout| upon success.
//
std::optional<vk::UniqueDeviceMemory> TransitionToHostVisibleImage(
    const vk::PhysicalDevice& physical_device, const vk::Device& device, const vk::Image& src_image,
    const vk::Extent2D& extent, const vk::CommandPool& command_pool, const vk::Queue& queue,
    vk::SubresourceLayout* host_image_layout);

//
// ReadPixels
//
// Transitions |src_image| into a host-visible, R8G8B8A8Unorm linear image that can be mapped
// and read.  Copies the rectangle of pixels defined by |size| at |offset| pixels from the
// host image buffer into |pixels|.  Unmaps the host image memory before returning.
//
// Resizes |pixels| if |pixels->size()| isn't large enough to accommodate the copied pixels.
//
bool ReadPixels(const vk::PhysicalDevice& physical_device, const vk::Device& device,
                const vk::Image& src_image, const vk::Extent2D& src_image_size,
                const vk::CommandPool& command_pool, const vk::Queue& queue,
                const vk::Extent2D& size, const vk::Offset2D& offset,
                std::vector<uint32_t>* pixels);

}  // namespace vkp

#endif  // SRC_GRAPHICS_EXAMPLES_VKPROTO_COMMON_READBACK_H_
