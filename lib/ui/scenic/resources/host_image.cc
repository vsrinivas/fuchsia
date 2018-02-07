// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/resources/host_image.h"

#include "garnet/lib/ui/scenic/engine/session.h"
#include "garnet/lib/ui/scenic/resources/gpu_memory.h"
#include "garnet/lib/ui/scenic/resources/host_memory.h"
#include "lib/escher/util/image_utils.h"

namespace scene_manager {

const ResourceTypeInfo HostImage::kTypeInfo = {
    ResourceType::kHostImage | ResourceType::kImage | ResourceType::kImageBase,
    "HostImage"};

HostImage::HostImage(Session* session,
                     scenic::ResourceId id,
                     HostMemoryPtr memory,
                     escher::ImagePtr image,
                     uint64_t host_memory_offset)
    : Image(session, id, HostImage::kTypeInfo),
      memory_(std::move(memory)),
      memory_offset_(host_memory_offset) {
  image_ = std::move(image);
}

ImagePtr HostImage::New(Session* session,
                        scenic::ResourceId id,
                        HostMemoryPtr host_memory,
                        const scenic::ImageInfoPtr& image_info,
                        uint64_t memory_offset,
                        ErrorReporter* error_reporter) {
  vk::Format pixel_format = vk::Format::eUndefined;
  size_t bytes_per_pixel;
  size_t pixel_alignment;
  switch (image_info->pixel_format) {
    case scenic::ImageInfo::PixelFormat::BGRA_8:
      pixel_format = vk::Format::eB8G8R8A8Unorm;
      bytes_per_pixel = 4u;
      pixel_alignment = 4u;
      break;
  }

  if (image_info->width <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (image_info->height <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->engine()->escher()->device()->caps();
  if (image_info->width > caps.max_image_width) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image width exceeds maximum ("
        << image_info->width << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (image_info->height > caps.max_image_height) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image height exceeds maximum ("
        << image_info->height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  if (image_info->stride < image_info->width * bytes_per_pixel) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): stride too small for width.";
    return nullptr;
  }
  if (image_info->stride % pixel_alignment != 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): stride must preserve pixel alignment.";
    return nullptr;
  }
  if (image_info->tiling != scenic::ImageInfo::Tiling::LINEAR) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): tiling must be LINEAR for images "
        << "created using host memory.";
    return nullptr;
  }

  size_t image_size = image_info->height * image_info->stride;
  if (memory_offset >= host_memory->size()) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): the offset of the Image must be "
        << "within the range of the Memory";
    return nullptr;
  }

  if (memory_offset + image_size > host_memory->size()) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): the Image must fit within the size "
        << "of the Memory";
    return nullptr;
  }

  // TODO(MZ-141): Support non-minimal strides.
  if (image_info->stride != image_info->width * bytes_per_pixel) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): the stride must be minimal (MZ-141)";
    return nullptr;
  }

  auto escher_image = escher::image_utils::NewImageFromPixels(
      session->engine()->escher_image_factory(),
      session->engine()->escher_gpu_uploader(),
      static_cast<uint8_t*>(host_memory->memory_base()) + memory_offset,
      pixel_format, image_info->width, image_info->height);

  return fxl::AdoptRef(new HostImage(session, id, std::move(host_memory),
                                     std::move(escher_image), memory_offset));
}

bool HostImage::UpdatePixels() {
  if (session()->engine()->escher_gpu_uploader()) {
    escher::image_utils::WritePixelsToImage(
        session()->engine()->escher_gpu_uploader(),
        static_cast<uint8_t*>(memory_->memory_base()) + memory_offset_, image_);
    return true;
  }
  return false;
}

ImagePtr HostImage::NewForTesting(Session* session,
                                  scenic::ResourceId id,
                                  escher::ResourceManager* image_owner,
                                  HostMemoryPtr host_memory) {
  escher::ImagePtr escher_image = escher::Image::New(
      image_owner, escher::ImageInfo(), vk::Image(), nullptr);
  FXL_CHECK(escher_image);
  return fxl::AdoptRef(
      new HostImage(session, id, host_memory, escher_image, 0));
}

}  // namespace scene_manager
