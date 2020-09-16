// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_UTIL_IMAGE_UTILS_H_
#define SRC_UI_LIB_ESCHER_UTIL_IMAGE_UTILS_H_

#ifdef __Fuchsia__
#include <lib/fit/function.h>
#endif

#include <utility>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/forward_declarations.h"
#include "src/ui/lib/escher/vk/image.h"

namespace escher {
class BatchGpuUploader;
class ImageFactory;

namespace image_utils {

using ImageConversionFunction = fit::function<void(void*, const void*, uint32_t, uint32_t)>;

// Returns the number of bytes per pixel for the given format.
size_t BytesPerPixel(vk::Format format);

// Return true if |format| can be used as a depth buffer.
bool IsDepthFormat(vk::Format format);

// Return true if |format| can be used as a stencil buffer.
bool IsStencilFormat(vk::Format format);

// Return true is |format| is one of the formats that Escher can treat as a YUV format.
// Currently these include:
//   - eG8B8G8R8422Unorm
//   - eG8B8R82Plane420Unorm
// TODO(SCN-1403): use of these formats is not enough to assume NV12, but they're currently the
// only formats we support at the sampler level.
bool IsYuvFormat(vk::Format format);

// Return a pair of booleans, each of which is true if |format| can be used as
// a depth or stencil buffer, respectively.
std::pair<bool, bool> IsDepthStencilFormat(vk::Format format);

// If |format| is a depth-stencil format, return the appropriate combination
// of eDepth and eStencil bits.  Otherwise, treat it as a color format, and
// return eColor.
vk::ImageAspectFlags FormatToColorOrDepthStencilAspectFlags(vk::Format format);

vk::ImageCreateInfo CreateVkImageCreateInfo(ImageInfo info, vk::ImageLayout initial_layout);

// Helper function that creates a VkImage given the parameters in ImageInfo.
// This does not bind the VkImage to memory; the caller must do that
// separately after calling this function.
vk::Image CreateVkImage(const vk::Device& device, ImageInfo info, vk::ImageLayout initial_layout);

// Return a new Image that is suitable for use as a depth attachment.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewDepthImage(ImageFactory* image_factory, vk::Format format, uint32_t width,
                       uint32_t height, vk::ImageUsageFlags additional_flags);

// Return a new Image that is suitable for use as a color attachment.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewColorAttachmentImage(ImageFactory* image_factory, uint32_t width, uint32_t height,
                                 vk::ImageUsageFlags additional_flags);

// Returns new image bound to the provided gpu memory, with the specified create info. This
// should only be called in a scenario where we know the |gpu_mem| buffer passed in will be
// compatible with the Vulkan image memory requirements such as being of a sufficiently large
// size to back the image. Otherwise, this function returns nullptr.
ImagePtr NewImage(const vk::Device& device, const vk::ImageCreateInfo& create_info,
                  escher::GpuMemPtr gpu_mem, escher::ResourceRecycler* resource_recycler);

// Return new Image containing the provided pixels. Uses transfer queue to
// efficiently transfer image data to GPU.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewImage(ImageFactory* image_factory, vk::Format format, uint32_t width, uint32_t height,
                  vk::ImageUsageFlags additional_flags = vk::ImageUsageFlags(),
                  vk::MemoryPropertyFlags memory_flags = vk::MemoryPropertyFlags());

// Write the contents of |pixels| into an existing |gpu_image|.
// The width, and height of |pixels| is assumed to match that of
// |gpu_image|.
// If the format of |pixels| is different from |gpu_image|, a conversion
// function that can convert from |pixels| to |gpu_image| should be
// provided as |convertion_func|.
// Note that all escher images are stored in GPU memory.  The use of gpu_image
// here is to specifically differentiate it from pixels, which may be stored in
// host_memory.
void WritePixelsToImage(
    escher::BatchGpuUploader* batch_gpu_uploader, const uint8_t* pixels, const ImagePtr& gpu_image,
    vk::ImageLayout final_layout = vk::ImageLayout::eShaderReadOnlyOptimal,
    const escher::image_utils::ImageConversionFunction& convertion_func = nullptr);

// Return new Image containing the provided pixels.  Uses transfer queue to
// efficiently transfer image data to GPU.  If bytes is null, don't bother
// transferring.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewRgbaImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader, uint32_t width,
                      uint32_t height, const uint8_t* bytes,
                      vk::ImageLayout final_layout = vk::ImageLayout::eShaderReadOnlyOptimal);

// Returns RGBA image.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewCheckerboardImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader,
                              uint32_t width, uint32_t height);

// Returns RGBA image.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewGradientImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader,
                          uint32_t width, uint32_t height);

// Returns single-channel luminance image containing white noise.
// |image_factory| is a generic interface that could be an Image cache (in which
// case a new Image might be created, or an existing one reused). Alternatively
// the factory could allocate a new Image every time.
ImagePtr NewNoiseImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader, uint32_t width,
                       uint32_t height,
                       vk::ImageUsageFlags additional_flags = vk::ImageUsageFlags());

// Return RGBA pixels containing a checkerboard pattern, where each white/black
// region is a single pixel.  Only works for even values of width/height.
std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width, uint32_t height,
                                                 size_t* out_size = nullptr);

// Return RGBA pixels containing a gradient where the top row is white and the
// bottom row is black.  Only works for even values of width/height.
std::unique_ptr<uint8_t[]> NewGradientPixels(uint32_t width, uint32_t height,
                                             size_t* out_size = nullptr);

// Return eR8Unorm pixels containing random noise.
std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width, uint32_t height,
                                          size_t* out_size = nullptr);

// Creates a vk::ImageCreateInfo with common default values and |vk_format|.
vk::ImageCreateInfo GetDefaultImageConstraints(const vk::Format& vk_format);

}  // namespace image_utils
}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_UTIL_IMAGE_UTILS_H_
