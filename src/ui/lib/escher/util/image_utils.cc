// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/util/image_utils.h"

#include <random>

#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/impl/vulkan_utils.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/lib/escher/vk/image_factory.h"

#include <vulkan/vulkan.hpp>

namespace {
struct RGBA {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

constexpr VkExternalMemoryImageCreateInfo kExternalImageCreateInfo{
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .pNext = nullptr,
#ifdef __Fuchsia__
    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_TEMP_ZIRCON_VMO_BIT_FUCHSIA,
#else
    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
#endif
};
}  // namespace

namespace escher {
namespace image_utils {

size_t BytesPerPixel(vk::Format format) {
  switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eB8G8R8A8Unorm:
    case vk::Format::eR8G8B8A8Srgb:
    case vk::Format::eB8G8R8A8Srgb:
      return 4;
    case vk::Format::eG8B8G8R8422Unorm:
    case vk::Format::eG8B8R82Plane420Unorm:
      return 2;
    case vk::Format::eR8Unorm:
      return 1;
    default:
      FX_CHECK(false);
      return 0;
  }
}

bool IsDepthFormat(vk::Format format) {
  switch (format) {
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      return true;
    default:
      return false;
  }
}

bool IsStencilFormat(vk::Format format) {
  switch (format) {
    case vk::Format::eS8Uint:
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      return true;
    default:
      return false;
  }
}

std::pair<bool, bool> IsDepthStencilFormat(vk::Format format) {
  switch (format) {
    case vk::Format::eD16UnormS8Uint:
    case vk::Format::eD24UnormS8Uint:
    case vk::Format::eD32SfloatS8Uint:
      return {true, true};
    case vk::Format::eD16Unorm:
    case vk::Format::eX8D24UnormPack32:
    case vk::Format::eD32Sfloat:
      return {true, false};
    case vk::Format::eS8Uint:
      return {false, true};
    default:
      return {false, false};
  }
}

bool IsYuvFormat(vk::Format format) {
  switch (format) {
    case vk::Format::eG8B8G8R8422Unorm:
    case vk::Format::eG8B8R82Plane420Unorm:
    case vk::Format::eG8B8R83Plane420Unorm:
      return true;
    default:
      return false;
  }
}

vk::ImageAspectFlags FormatToColorOrDepthStencilAspectFlags(vk::Format format) {
  const auto is_depth_stencil = IsDepthStencilFormat(format);
  const bool is_depth = is_depth_stencil.first;
  const bool is_stencil = is_depth_stencil.second;

  if (is_depth) {
    // Maybe also stencil?
    return is_stencil ? vk::ImageAspectFlagBits::eDepth | vk::ImageAspectFlagBits::eStencil
                      : vk::ImageAspectFlagBits::eDepth;
  } else if (is_stencil) {
    // Only stencil, not depth.
    return vk::ImageAspectFlagBits::eStencil;
  } else {
    // Neither depth nor stencil.
    return vk::ImageAspectFlagBits::eColor;
  }
}

vk::ImageCreateInfo CreateVkImageCreateInfo(ImageInfo info, vk::ImageLayout initial_layout) {
  // Per Vulkan spec, for new images the layout should be only ePreinitialized or eUndefined.
  FX_CHECK(initial_layout == vk::ImageLayout::ePreinitialized ||
           initial_layout == vk::ImageLayout::eUndefined);

  vk::ImageCreateInfo create_info;
  create_info.pNext = info.is_external ? &kExternalImageCreateInfo : nullptr;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = info.format;
  create_info.extent = vk::Extent3D{info.width, info.height, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = impl::SampleCountFlagBitsFromInt(info.sample_count);
  create_info.tiling = info.tiling;
  create_info.usage = info.usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = initial_layout;
  create_info.flags =
      info.is_mutable ? vk::ImageCreateFlagBits::eMutableFormat : vk::ImageCreateFlags();
  if (info.memory_flags & vk::MemoryPropertyFlagBits::eProtected) {
    create_info.flags |= vk::ImageCreateFlagBits::eProtected;
  }
  return create_info;
}

vk::Image CreateVkImage(const vk::Device& device, ImageInfo info, vk::ImageLayout initial_layout) {
  vk::Image image =
      ESCHER_CHECKED_VK_RESULT(device.createImage(CreateVkImageCreateInfo(info, initial_layout)));
  return image;
}

ImagePtr NewDepthImage(ImageFactory* image_factory, vk::Format format, uint32_t width,
                       uint32_t height, vk::ImageUsageFlags additional_flags) {
  FX_DCHECK(image_factory);
  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eDepthStencilAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewColorAttachmentImage(ImageFactory* image_factory, uint32_t width, uint32_t height,
                                 vk::ImageUsageFlags additional_flags) {
  FX_DCHECK(image_factory);
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eColorAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewImage(const vk::Device& device, const vk::ImageCreateInfo& create_info,
                  escher::GpuMemPtr gpu_mem, escher::ResourceRecycler* resource_recycler) {
  FX_DCHECK(resource_recycler);
  FX_DCHECK(gpu_mem);

  // Vulkan.hpp DCHECKs if this is false, so don't do any extra checking at this point here.
  auto image_result = device.createImage(create_info);

  // Make sure that the image is within range of its associated memory.
  vk::MemoryRequirements memory_reqs;
  device.getImageMemoryRequirements(image_result.value, &memory_reqs);
  if (memory_reqs.size > gpu_mem->size()) {
    FX_LOGS(ERROR) << "Memory requirements for image exceed available memory: " << memory_reqs.size
                   << " " << gpu_mem->size();
    return nullptr;
  }

  escher::ImageInfo image_info;
  image_info.format = create_info.format;
  image_info.width = create_info.extent.width;
  image_info.height = create_info.extent.height;
  image_info.usage = create_info.usage;
  image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
  if (create_info.flags & vk::ImageCreateFlagBits::eProtected) {
    image_info.memory_flags = vk::MemoryPropertyFlagBits::eProtected;
  }
  image_info.is_external = true;

  return escher::impl::NaiveImage::AdoptVkImage(resource_recycler, image_info, image_result.value,
                                                std::move(gpu_mem), create_info.initialLayout);
}

ImagePtr NewImage(ImageFactory* image_factory, vk::Format format, uint32_t width, uint32_t height,
                  vk::ImageUsageFlags additional_flags, vk::MemoryPropertyFlags memory_flags) {
  FX_DCHECK(image_factory);

  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
  info.memory_flags |= memory_flags;

  // Create the new image.
  auto image = image_factory->NewImage(info);

  return image;
}

void WritePixelsToImage(BatchGpuUploader* batch_gpu_uploader, const uint8_t* pixels,
                        const ImagePtr& image, vk::ImageLayout final_layout,
                        const ImageConversionFunction& conversion_func) {
  FX_DCHECK(batch_gpu_uploader);
  FX_DCHECK(image);
  FX_DCHECK(pixels);

  size_t bytes_per_pixel = BytesPerPixel(image->info().format);
  size_t width = image->info().width;
  size_t height = image->info().height;

  std::vector<uint8_t> pixels_to_write(width * height * bytes_per_pixel);
  if (!conversion_func) {
    std::copy(pixels, pixels + width * height * bytes_per_pixel, pixels_to_write.begin());
  } else {
    conversion_func(pixels_to_write.data(), pixels, width, height);
  }

  vk::BufferImageCopy region;
  region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageExtent.width = width;
  region.imageExtent.height = height;
  region.imageExtent.depth = 1;
  region.bufferOffset = 0;

  batch_gpu_uploader->ScheduleWriteImage(image, std::move(pixels_to_write), final_layout, region);
}

ImagePtr NewRgbaImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader, uint32_t width,
                      uint32_t height, const uint8_t* pixels, vk::ImageLayout final_layout) {
  FX_DCHECK(image_factory);
  FX_DCHECK(gpu_uploader);

  auto image =
      NewImage(image_factory, vk::Format::eR8G8B8A8Unorm, width, height, vk::ImageUsageFlags());

  WritePixelsToImage(gpu_uploader, pixels, image, final_layout);
  return image;
}

ImagePtr NewCheckerboardImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader,
                              uint32_t width, uint32_t height) {
  FX_DCHECK(image_factory);
  FX_DCHECK(gpu_uploader);

  auto image = NewImage(image_factory, vk::Format::eR8G8B8A8Unorm, width, height);

  auto pixels = NewCheckerboardPixels(width, height);
  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

ImagePtr NewGradientImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader,
                          uint32_t width, uint32_t height) {
  FX_DCHECK(image_factory);
  FX_DCHECK(gpu_uploader);

  auto pixels = NewGradientPixels(width, height);
  // TODO(SCN-839): are SRGB formats slow on Mali?
  auto image = NewImage(image_factory, vk::Format::eR8G8B8A8Srgb, width, height);

  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

ImagePtr NewNoiseImage(ImageFactory* image_factory, BatchGpuUploader* gpu_uploader, uint32_t width,
                       uint32_t height, vk::ImageUsageFlags additional_flags) {
  FX_DCHECK(image_factory);
  FX_DCHECK(gpu_uploader);

  auto pixels = NewNoisePixels(width, height);
  auto image = NewImage(image_factory, vk::Format::eR8Unorm, width, height, additional_flags);

  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width, uint32_t height,
                                                 size_t* out_size) {
  FX_DCHECK(width % 2 == 0);
  FX_DCHECK(height % 2 == 0);

  size_t size_in_bytes = width * height * sizeof(RGBA);
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  if (out_size) {
    (*out_size) = size_in_bytes;
  }
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  for (uint32_t j = 0; j < height; ++j) {
    for (uint32_t i = 0; i < width; ++i) {
      uint32_t index = j * width + i;
      auto& p = pixels[index];
      p.r = p.g = p.b = (i + j) % 2 ? 0 : 255;
      p.a = 255;
    }
  }

  return ptr;
}

std::unique_ptr<uint8_t[]> NewGradientPixels(uint32_t width, uint32_t height, size_t* out_size) {
  FX_DCHECK(width % 2 == 0);
  FX_DCHECK(height % 2 == 0);

  size_t size_in_bytes = width * height * sizeof(RGBA);
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  if (out_size) {
    (*out_size) = size_in_bytes;
  }
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  float intensity_step = 255.0001f / (height - 1);
  for (uint32_t j = 0; j < height; ++j) {
    uint32_t intensity = 255.f - j * intensity_step;
    for (uint32_t i = 0; i < width; ++i) {
      uint32_t index = j * width + i;
      auto& p = pixels[index];
      p.r = p.g = p.b = intensity;
      p.a = 255;
    }
  }

  return ptr;
}

std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width, uint32_t height, size_t* out_size) {
  size_t size_in_bytes = width * height;
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  if (out_size) {
    (*out_size) = size_in_bytes;
  }

  auto pixels = ptr.get();

// TODO: when we have a random source, use it.
#if defined(__Fuchsia__)
  std::default_random_engine prng(12345);
#else
  std::random_device seed;
  std::default_random_engine prng(seed());
#endif
  std::uniform_int_distribution<uint8_t> random;

  for (uint32_t j = 0; j < height; ++j) {
    for (uint32_t i = 0; i < width; ++i) {
      pixels[j * width + i] = random(prng);
    }
  }

  return ptr;
}

vk::ImageCreateInfo GetDefaultImageConstraints(const vk::Format& vk_format) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.extent = vk::Extent3D{1, 1, 1};
  create_info.flags = vk::ImageCreateFlags();
  create_info.format = vk_format;
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  return create_info;
}

}  // namespace image_utils
}  // namespace escher
