// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/host_image.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/util/image_formats.h"
#include "lib/images/cpp/images.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo HostImage::kTypeInfo = {
    ResourceType::kHostImage | ResourceType::kImage | ResourceType::kImageBase,
    "HostImage"};

HostImage::HostImage(Session* session, scenic::ResourceId id,
                     HostMemoryPtr memory, escher::ImagePtr image,
                     uint64_t host_memory_offset,
                     fuchsia::images::ImageInfo host_image_format)
    : Image(session, id, HostImage::kTypeInfo),
      memory_(std::move(memory)),
      memory_offset_(host_memory_offset),
      host_image_format_(host_image_format) {
  image_ = std::move(image);
  image_conversion_function_ =
      image_formats::GetFunctionToConvertToBgra8(host_image_format);
}

ImagePtr HostImage::New(Session* session, scenic::ResourceId id,
                        HostMemoryPtr host_memory,
                        const fuchsia::images::ImageInfo& host_image_info,
                        uint64_t memory_offset, ErrorReporter* error_reporter) {
  // No matter what the incoming format, the gpu format will be BGRA:
  vk::Format gpu_image_pixel_format = vk::Format::eB8G8R8A8Unorm;
  size_t bits_per_pixel = images::BitsPerPixel(host_image_info.pixel_format);
  size_t pixel_alignment =
      images::MaxSampleAlignment(host_image_info.pixel_format);

  if (host_image_info.width <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (host_image_info.height <= 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->engine()->escher()->device()->caps();
  if (host_image_info.width > caps.max_image_width) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image width exceeds maximum ("
        << host_image_info.width << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (host_image_info.height > caps.max_image_height) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): image height exceeds maximum ("
        << host_image_info.height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  uint64_t width_bytes = (host_image_info.width * bits_per_pixel + 7) / 8;
  if (host_image_info.stride < width_bytes) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): stride too small for width.";
    return nullptr;
  }
  if (host_image_info.stride % pixel_alignment != 0) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): stride must preserve pixel alignment.";
    return nullptr;
  }
  if (host_image_info.tiling != fuchsia::images::Tiling::LINEAR) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): tiling must be LINEAR for images "
        << "created using host memory.";
    return nullptr;
  }

  size_t image_size = images::ImageSize(host_image_info);
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

  // TODO(SCN-141): Support non-minimal strides for all formats.  For now, NV12
  // is ok because it will have image_conversion_function_ and for formats with
  // image_conversion_function_, the stride is really only the input data stride
  // not the output data stride (which ends up being minimal thanks to the
  // image_conversion_function_).
  if (host_image_info.pixel_format != fuchsia::images::PixelFormat::NV12 &&
      host_image_info.stride != width_bytes) {
    error_reporter->ERROR()
        << "Image::CreateFromMemory(): the stride must be minimal (SCN-141)";
    return nullptr;
  }

  auto escher_image = escher::image_utils::NewImage(
      session->engine()->escher_image_factory(), gpu_image_pixel_format,
      host_image_info.width, host_image_info.height);

  auto host_image = fxl::AdoptRef(
      new HostImage(session, id, std::move(host_memory),
                    std::move(escher_image), memory_offset, host_image_info));
  host_image->UpdatePixels();
  return host_image;
}

bool HostImage::UpdatePixels() {
  if (session()->engine()->escher_gpu_uploader()) {
    escher::image_utils::WritePixelsToImage(
        session()->engine()->escher_gpu_uploader(),
        static_cast<uint8_t*>(memory_->memory_base()) + memory_offset_, image_,
        image_conversion_function_);
    return true;
  }
  return false;
}

ImagePtr HostImage::NewForTesting(Session* session, scenic::ResourceId id,
                                  escher::ResourceManager* image_owner,
                                  HostMemoryPtr host_memory) {
  escher::ImagePtr escher_image = escher::Image::New(
      image_owner, escher::ImageInfo(), vk::Image(), nullptr);
  FXL_CHECK(escher_image);
  fuchsia::images::ImageInfo host_image_format;
  host_image_format.pixel_format = fuchsia::images::PixelFormat::BGRA_8;
  return fxl::AdoptRef(new HostImage(session, id, host_memory, escher_image, 0,
                                     host_image_format));
}

}  // namespace gfx
}  // namespace scenic
