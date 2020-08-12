// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/host_image.h"

#include <lib/trace/event.h>

#include "lib/images/cpp/images.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/util/image_formats.h"

namespace {
// TODO(SCN-1387): This number needs to be queried via sysmem or vulkan.
constexpr uint32_t kYuvStrideRequirement = 64;
}  // namespace

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo HostImage::kTypeInfo = {
    ResourceType::kHostImage | ResourceType::kImage | ResourceType::kImageBase, "HostImage"};

HostImage::HostImage(Session* session, ResourceId id, MemoryPtr memory, escher::ImagePtr image,
                     uint64_t memory_offset, fuchsia::images::ImageInfo image_format)
    : Image(session, id, HostImage::kTypeInfo),
      memory_(std::move(memory)),
      memory_offset_(memory_offset),
      image_format_(image_format) {
  image_ = std::move(image);
  image_conversion_function_ = image_formats::GetFunctionToConvertToBgra8(image_format);
}

ImagePtr HostImage::New(Session* session, ResourceId id, MemoryPtr memory,
                        const fuchsia::images::ImageInfo& image_info, uint64_t memory_offset,
                        ErrorReporter* error_reporter) {
  if (image_info.pixel_format == fuchsia::images::PixelFormat::R8G8B8A8) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): pixelformat must not be R8G8B8A8.";
    return nullptr;
  }
  // No matter what the incoming format, the gpu format will be BGRA:
  vk::Format gpu_image_pixel_format = vk::Format::eB8G8R8A8Srgb;
  size_t pixel_alignment = images::MaxSampleAlignment(image_info.pixel_format);

  if (image_info.width <= 0) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): width must be greater than 0.";
    return nullptr;
  }
  if (image_info.height <= 0) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->resource_context().vk_device_queues_capabilities;
  if (image_info.width > caps.max_image_width) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): image width exceeds maximum ("
                            << image_info.width << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (image_info.height > caps.max_image_height) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): image height exceeds maximum ("
                            << image_info.height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  uint64_t width_bytes =
      image_info.width * images::StrideBytesPerWidthPixel(image_info.pixel_format);
  if (image_info.stride < width_bytes) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): stride too small for width";
    return nullptr;
  }
  if (image_info.stride % pixel_alignment != 0) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): stride must preserve pixel alignment.";
    return nullptr;
  }
  if (image_info.tiling != fuchsia::images::Tiling::LINEAR) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): tiling must be LINEAR for images "
                            << "created using host memory.";
    return nullptr;
  }
  // TODO(fxbug.dev/47918): Support non-premultiplied alpha format and remove this.
  if (image_info.alpha_format == fuchsia::images::AlphaFormat::NON_PREMULTIPLIED) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): Non-premultiplied alpha format "
                            << "is not supported yet.";
    return nullptr;
  }

  size_t image_size = images::ImageSize(image_info);
  if (memory_offset >= memory->size()) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): the offset of the Image must be "
                            << "within the range of the Memory";
    return nullptr;
  }
  if (memory_offset + image_size > memory->size()) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): the Image must fit within the size "
                            << "of the Memory";
    return nullptr;
  }

  // TODO(fxbug.dev/43039): Directly mapped images actually work as GpuImage, and they
  // should be created as GpuImage as well.
  if (image_info.pixel_format == fuchsia::images::PixelFormat::NV12 &&
      image_info.stride % kYuvStrideRequirement == 0) {
    // If we are not on a UMA platform, GetGpuMem will return a null pointer.
    auto gpu_memory = memory->GetGpuMem(error_reporter);
    if (gpu_memory) {
      escher::ImageInfo escher_image_info;
      escher_image_info.format = vk::Format::eG8B8R82Plane420Unorm;
      escher_image_info.width = image_info.width;
      escher_image_info.height = image_info.height;
      escher_image_info.sample_count = 1;
      escher_image_info.usage = vk::ImageUsageFlagBits::eSampled;
      escher_image_info.tiling = vk::ImageTiling::eLinear;
      escher_image_info.is_mutable = false;
      escher_image_info.is_external = true;
      // TODO(SCN-1012): This code assumes that Memory::GetGpuMem() will only
      // return device local memory.
      escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

      constexpr auto kInitialLayout = vk::ImageLayout::ePreinitialized;
      vk::Image vk_image = escher::image_utils::CreateVkImage(session->resource_context().vk_device,
                                                              escher_image_info, kInitialLayout);
      auto escher_image = escher::impl::NaiveImage::AdoptVkImage(
          session->resource_context().escher_resource_recycler, escher_image_info, vk_image,
          gpu_memory, kInitialLayout);

      if (!escher_image) {
        error_reporter->ERROR() << "Image::CreateFromMemory(): cannot create NaiveImage.";
        return nullptr;
      }

      auto host_image = fxl::AdoptRef(new HostImage(
          session, id, std::move(memory), std::move(escher_image), memory_offset, image_info));
      host_image->is_directly_mapped_ = true;
      // Directly-mapped images are never dirty.
      host_image->dirty_ = false;
      return host_image;
    }
  }

  // TODO(SCN-141): Support non-minimal strides for all formats.  For now, NV12
  // is ok because it will have image_conversion_function_ and for formats with
  // image_conversion_function_, the stride is really only the input data stride
  // not the output data stride (which ends up being minimal thanks to the
  // image_conversion_function_).
  if (image_info.pixel_format != fuchsia::images::PixelFormat::NV12 &&
      image_info.stride != width_bytes) {
    error_reporter->ERROR() << "Image::CreateFromMemory(): the stride must be minimal (SCN-141)";
    return nullptr;
  }

  auto escher_image =
      escher::image_utils::NewImage(session->resource_context().escher_image_factory,
                                    gpu_image_pixel_format, image_info.width, image_info.height);

  auto host_image = fxl::AdoptRef(new HostImage(
      session, id, std::move(memory), std::move(escher_image), memory_offset, image_info));
  return host_image;
}

void HostImage::UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                                  escher::ImageLayoutUpdater* layout_updater) {
  if (is_directly_mapped_) {
    // Directly mapped host images are never dirty. So this function should do
    // nothing unless we need to update the image layout.
    dirty_ = false;
    if (!image_->is_layout_initialized()) {
      if (layout_updater) {
        layout_updater->ScheduleSetImageInitialLayout(image_, vk::ImageLayout::eGeneral);
      } else {
        FX_LOGS(WARNING) << "No ImageLayoutUpdater, cannot set up image layout.";
      }
    }
  } else {
    // We only update the pixels if image is dirty.
    if (dirty_) {
      dirty_ = UpdatePixels(gpu_uploader);
    }
  }
}

bool HostImage::UpdatePixels(escher::BatchGpuUploader* gpu_uploader) {
  if (is_directly_mapped_) {
    // Directly-mapped images are never dirty.
    FX_CHECK(!is_directly_mapped_) << "Directly-mapped host images should never be dirty.";
    return false;
  }

  if (!gpu_uploader) {
    FX_LOGS(WARNING) << "No BatchGpuUploader, cannot UpdatePixels.";
    return true;
  }

  TRACE_DURATION("gfx", "UpdatePixels");
  escher::image_utils::WritePixelsToImage(
      gpu_uploader, static_cast<uint8_t*>(memory_->host_ptr()) + memory_offset_, image_,
      vk::ImageLayout::eShaderReadOnlyOptimal, image_conversion_function_);
  // Pixels have been updated, dirty state is now false.
  return false;
}

}  // namespace gfx
}  // namespace scenic_impl
