// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/image_utils.h"

#include <random>

#include "lib/escher/impl/gpu_uploader.h"
#include "lib/escher/impl/vulkan_utils.h"
#include "lib/escher/vk/gpu_mem.h"
#include "lib/escher/vk/image_factory.h"

namespace {
struct RGBA {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};
}  // namespace

namespace escher {
namespace image_utils {

size_t BytesPerPixel(vk::Format format) {
  switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eB8G8R8A8Unorm:
      return 4;
    case vk::Format::eR8Unorm:
      return 1;
    default:
      FXL_CHECK(false);
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

vk::ImageAspectFlags FormatToColorOrDepthStencilAspectFlags(vk::Format format) {
  const auto is_depth_stencil = IsDepthStencilFormat(format);
  const bool is_depth = is_depth_stencil.first;
  const bool is_stencil = is_depth_stencil.second;

  if (is_depth) {
    // Maybe also stencil?
    return is_stencil ? vk::ImageAspectFlagBits::eDepth |
                            vk::ImageAspectFlagBits::eStencil
                      : vk::ImageAspectFlagBits::eDepth;
  } else if (is_stencil) {
    // Only stencil, not depth.
    return vk::ImageAspectFlagBits::eStencil;
  } else {
    // Neither depth nor stencil.
    return vk::ImageAspectFlagBits::eColor;
  }
}

vk::Image CreateVkImage(const vk::Device& device, ImageInfo info) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = info.format;
  create_info.extent = vk::Extent3D{info.width, info.height, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = impl::SampleCountFlagBitsFromInt(info.sample_count);
  create_info.tiling = info.tiling;
  create_info.usage = info.usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device.createImage(create_info));
  return image;
}

ImagePtr NewDepthImage(ImageFactory* image_factory, vk::Format format,
                       uint32_t width, uint32_t height,
                       vk::ImageUsageFlags additional_flags) {
  FXL_DCHECK(image_factory);
  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage =
      additional_flags | vk::ImageUsageFlagBits::eDepthStencilAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewColorAttachmentImage(ImageFactory* image_factory, uint32_t width,
                                 uint32_t height,
                                 vk::ImageUsageFlags additional_flags) {
  FXL_DCHECK(image_factory);
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eColorAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewImage(ImageFactory* image_factory, vk::Format format,
                  uint32_t width, uint32_t height,
                  vk::ImageUsageFlags additional_flags) {
  FXL_DCHECK(image_factory);

  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eSampled;

  // Create the new image.
  auto image = image_factory->NewImage(info);

  return image;
}

void WritePixelsToImage(impl::GpuUploader* gpu_uploader, uint8_t* pixels,
                        ImagePtr image,
                        const ImageConversionFunction& conversion_func) {
  FXL_DCHECK(gpu_uploader);
  FXL_DCHECK(image);
  FXL_DCHECK(pixels);

  size_t bytes_per_pixel = BytesPerPixel(image->info().format);
  size_t width = image->info().width;
  size_t height = image->info().height;

  auto writer = gpu_uploader->GetWriter(width * height * bytes_per_pixel);
  if (!conversion_func) {
    std::memcpy(writer.ptr(), pixels, width * height * bytes_per_pixel);
  } else {
    conversion_func(writer.ptr(), pixels, width, height);
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

  writer.WriteImage(image, region, Semaphore::New(gpu_uploader->device()));
  writer.Submit();
}

ImagePtr NewRgbaImage(ImageFactory* image_factory,
                      impl::GpuUploader* gpu_uploader, uint32_t width,
                      uint32_t height, uint8_t* pixels) {
  FXL_DCHECK(image_factory);
  FXL_DCHECK(gpu_uploader);

  auto image =
      NewImage(image_factory, vk::Format::eR8G8B8A8Unorm, width, height);

  WritePixelsToImage(gpu_uploader, pixels, image);
  return image;
}

ImagePtr NewCheckerboardImage(ImageFactory* image_factory,
                              impl::GpuUploader* gpu_uploader, uint32_t width,
                              uint32_t height) {
  FXL_DCHECK(image_factory);
  FXL_DCHECK(gpu_uploader);

  auto image =
      NewImage(image_factory, vk::Format::eR8G8B8A8Unorm, width, height);

  auto pixels = NewCheckerboardPixels(width, height);
  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

ImagePtr NewGradientImage(ImageFactory* image_factory,
                          impl::GpuUploader* gpu_uploader, uint32_t width,
                          uint32_t height) {
  FXL_DCHECK(image_factory);
  FXL_DCHECK(gpu_uploader);

  auto pixels = NewGradientPixels(width, height);
  auto image =
      NewImage(image_factory, vk::Format::eR8G8B8A8Unorm, width, height);

  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

ImagePtr NewNoiseImage(ImageFactory* image_factory,
                       impl::GpuUploader* gpu_uploader, uint32_t width,
                       uint32_t height, vk::ImageUsageFlags additional_flags) {
  FXL_DCHECK(image_factory);
  FXL_DCHECK(gpu_uploader);

  auto pixels = NewNoisePixels(width, height);
  auto image = NewImage(image_factory, vk::Format::eR8Unorm, width, height,
                        additional_flags);

  WritePixelsToImage(gpu_uploader, pixels.get(), image);
  return image;
}

std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width,
                                                 uint32_t height,
                                                 size_t* out_size) {
  FXL_DCHECK(width % 2 == 0);
  FXL_DCHECK(height % 2 == 0);

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

std::unique_ptr<uint8_t[]> NewGradientPixels(uint32_t width, uint32_t height,
                                             size_t* out_size) {
  FXL_DCHECK(width % 2 == 0);
  FXL_DCHECK(height % 2 == 0);

  size_t size_in_bytes = width * height * sizeof(RGBA);
  auto ptr = std::make_unique<uint8_t[]>(size_in_bytes);
  if (out_size) {
    (*out_size) = size_in_bytes;
  }
  RGBA* pixels = reinterpret_cast<RGBA*>(ptr.get());

  for (uint32_t j = 0; j < height; ++j) {
    uint32_t intensity = (height - j) * 255 / height;
    for (uint32_t i = 0; i < width; ++i) {
      uint32_t index = j * width + i;
      auto& p = pixels[index];
      p.r = p.g = p.b = intensity;
      p.a = 255;
    }
  }

  return ptr;
}

std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width, uint32_t height,
                                          size_t* out_size) {
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

}  // namespace image_utils
}  // namespace escher
