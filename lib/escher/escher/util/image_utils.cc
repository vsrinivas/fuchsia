// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/image_utils.h"

#include <random>

#include "escher/impl/gpu_uploader.h"
#include "escher/impl/vulkan_utils.h"
#include "escher/renderer/image_factory.h"
#include "escher/vk/gpu_mem.h"

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

vk::Image CreateVkImage(const vk::Device& device, ImageInfo info) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.format = info.format;
  create_info.extent = vk::Extent3D{info.width, info.height, 1};
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  switch (info.sample_count) {
    case 1:
      create_info.samples = vk::SampleCountFlagBits::e1;
      break;
    case 2:
      create_info.samples = vk::SampleCountFlagBits::e2;
      break;
    case 4:
      create_info.samples = vk::SampleCountFlagBits::e4;
      break;
    case 8:
      create_info.samples = vk::SampleCountFlagBits::e8;
      break;
    default:
      FTL_DCHECK(false);
  }
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = info.usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  vk::Image image = ESCHER_CHECKED_VK_RESULT(device.createImage(create_info));
  return image;
}

ImagePtr NewDepthImage(ImageFactory* image_factory,
                       vk::Format format,
                       uint32_t width,
                       uint32_t height,
                       vk::ImageUsageFlags additional_flags) {
  FTL_DCHECK(image_factory);
  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage =
      additional_flags | vk::ImageUsageFlagBits::eDepthStencilAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewColorAttachmentImage(ImageFactory* image_factory,
                                 uint32_t width,
                                 uint32_t height,
                                 vk::ImageUsageFlags additional_flags) {
  FTL_DCHECK(image_factory);
  ImageInfo info;
  info.format = vk::Format::eB8G8R8A8Unorm;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eColorAttachment;

  return image_factory->NewImage(info);
}

ImagePtr NewImageFromPixels(ImageFactory* image_factory,
                            impl::GpuUploader* gpu_uploader,
                            vk::Format format,
                            uint32_t width,
                            uint32_t height,
                            uint8_t* pixels,
                            vk::ImageUsageFlags additional_flags) {
  FTL_DCHECK(image_factory);
  FTL_DCHECK(gpu_uploader);

  size_t bytes_per_pixel = 0;
  switch (format) {
    case vk::Format::eR8G8B8A8Unorm:
    case vk::Format::eB8G8R8A8Unorm:
      bytes_per_pixel = 4;
      break;
    case vk::Format::eR8Unorm:
      bytes_per_pixel = 1;
      break;
    default:
      FTL_CHECK(false);
  }

  auto writer = gpu_uploader->GetWriter(width * height * bytes_per_pixel);
  memcpy(writer.ptr(), pixels, width * height * bytes_per_pixel);

  ImageInfo info;
  info.format = format;
  info.width = width;
  info.height = height;
  info.sample_count = 1;
  info.usage = additional_flags | vk::ImageUsageFlagBits::eTransferDst |
               vk::ImageUsageFlagBits::eSampled;

  // Create the new image.
  auto image = image_factory->NewImage(info);

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

  return image;
}

ImagePtr NewRgbaImage(ImageFactory* image_factory,
                      impl::GpuUploader* gpu_uploader,
                      uint32_t width,
                      uint32_t height,
                      uint8_t* pixels) {
  FTL_DCHECK(image_factory);
  FTL_DCHECK(gpu_uploader);

  return NewImageFromPixels(image_factory, gpu_uploader,
                            vk::Format::eR8G8B8A8Unorm, width, height, pixels);
}

ImagePtr NewCheckerboardImage(ImageFactory* image_factory,
                              impl::GpuUploader* gpu_uploader,
                              uint32_t width,
                              uint32_t height) {
  FTL_DCHECK(image_factory);
  FTL_DCHECK(gpu_uploader);

  auto pixels = NewCheckerboardPixels(width, height);
  return NewImageFromPixels(image_factory, gpu_uploader,
                            vk::Format::eR8G8B8A8Unorm, width, height,
                            pixels.get());
}

ImagePtr NewGradientImage(ImageFactory* image_factory,
                          impl::GpuUploader* gpu_uploader,
                          uint32_t width,
                          uint32_t height) {
  FTL_DCHECK(image_factory);
  FTL_DCHECK(gpu_uploader);

  auto pixels = NewGradientPixels(width, height);
  return NewImageFromPixels(image_factory, gpu_uploader,
                            vk::Format::eR8G8B8A8Unorm, width, height,
                            pixels.get());
}

ImagePtr NewNoiseImage(ImageFactory* image_factory,
                       impl::GpuUploader* gpu_uploader,
                       uint32_t width,
                       uint32_t height,
                       vk::ImageUsageFlags additional_flags) {
  FTL_DCHECK(image_factory);
  FTL_DCHECK(gpu_uploader);

  auto pixels = NewNoisePixels(width, height);
  return NewImageFromPixels(image_factory, gpu_uploader, vk::Format::eR8Unorm,
                            width, height, pixels.get(), additional_flags);
}

std::unique_ptr<uint8_t[]> NewCheckerboardPixels(uint32_t width,
                                                 uint32_t height,
                                                 size_t* out_size) {
  FTL_DCHECK(width % 2 == 0);
  FTL_DCHECK(height % 2 == 0);

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

std::unique_ptr<uint8_t[]> NewGradientPixels(uint32_t width,
                                             uint32_t height,
                                             size_t* out_size) {
  FTL_DCHECK(width % 2 == 0);
  FTL_DCHECK(height % 2 == 0);

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

std::unique_ptr<uint8_t[]> NewNoisePixels(uint32_t width,
                                          uint32_t height,
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
