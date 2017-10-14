// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/escher.h"
#include "lib/escher/forward_declarations.h"
#include "lib/escher/renderer/image.h"

namespace escher {
class ImageFactory;
namespace impl {
class GpuUploader;
}
namespace image_utils {

// Helper function that creates a VkImage given the parameters in ImageInfo.
// This does not bind the the VkImage to memory; the caller must do that
// separately after calling this function.
vk::Image CreateVkImage(const vk::Device& device, ImageInfo info);

// Return a new Image that is suitable for use as a depth attachment.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewDepthImage(ImageFactory* image_factory,
                       vk::Format format,
                       uint32_t width,
                       uint32_t height,
                       vk::ImageUsageFlags additional_flags);

// Return a new Image that is suitable for use as a color attachment.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewColorAttachmentImage(ImageFactory* image_factory,
                                 uint32_t width,
                                 uint32_t height,
                                 vk::ImageUsageFlags additional_flags);

// Return new Image containing the provided pixels. Uses transfer queue to
// efficiently transfer image data to GPU.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewImageFromPixels(
    ImageFactory* image_factory,
    impl::GpuUploader* gpu_uploader,
    vk::Format format,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    vk::ImageUsageFlags additional_flags = vk::ImageUsageFlags());

// Return new Image containing the provided pixels.  Uses transfer queue to
// efficiently transfer image data to GPU.  If bytes is null, don't bother
// transferring.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewRgbaImage(ImageFactory* image_factory,
                      impl::GpuUploader* gpu_uploader,
                      uint32_t width,
                      uint32_t height,
                      uint8_t* bytes);

// Returns RGBA image.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewCheckerboardImage(ImageFactory* image_factory,
                              impl::GpuUploader* gpu_uploader,
                              uint32_t width,
                              uint32_t height);

// Returns RGBA image.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewGradientImage(ImageFactory* image_factory,
                          impl::GpuUploader* gpu_uploader,
                          uint32_t width,
                          uint32_t height);

// Returns single-channel luminance image containing white noise.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewNoiseImage(
    ImageFactory* image_factory,
    impl::GpuUploader* gpu_uploader,
    uint32_t width,
    uint32_t height,
    vk::ImageUsageFlags additional_flags = vk::ImageUsageFlags());

// Return RGBA pixels containing a checkerboard pattern, where each white/black
// region is a single pixel.  Only works for even values of width/height.
std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width,
                                                 uint32_t height,
                                                 size_t* out_size = nullptr);

// Return RGBA pixels containing a gradient where the top row is white and the
// bottom row is black.  Only works for even values of width/height.
std::unique_ptr<uint8_t[]> NewGradientPixels(uint32_t width,
                                             uint32_t height,
                                             size_t* out_size = nullptr);

// Return eR8Unorm pixels containing random noise.
std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width,
                                          uint32_t height,
                                          size_t* out_size = nullptr);

}  // namespace image_utils
}  // namespace escher
