// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"

#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/image_layout_updater.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo GpuImage::kTypeInfo = {
    ResourceType::kGpuImage | ResourceType::kImage | ResourceType::kImageBase, "GpuImage"};

GpuImage::GpuImage(Session* session, ResourceId id, escher::ImagePtr image)
    : Image(session, id, GpuImage::kTypeInfo) {
  image_ = image;
}

GpuImagePtr GpuImage::New(Session* session, ResourceId id, MemoryPtr memory,
                          const fuchsia::images::ImageInfo& image_info, uint64_t memory_offset,
                          ErrorReporter* error_reporter) {
  vk::Format pixel_format = vk::Format::eUndefined;
  size_t bytes_per_pixel = 4u;
  size_t pixel_alignment = 4u;
  bool is_mutable = false;
  switch (image_info.pixel_format) {
    case fuchsia::images::PixelFormat::BGRA_8:
      pixel_format = vk::Format::eB8G8R8A8Srgb;
      // Mutable format flag is required for image info to match existing clients.
      // TODO(reveman): Remove this when clients have switched to immutable R8G8B8A8.
      is_mutable = true;
      break;
    case fuchsia::images::PixelFormat::R8G8B8A8:
      pixel_format = vk::Format::eR8G8B8A8Srgb;
      break;
    case fuchsia::images::PixelFormat::YUY2:
    case fuchsia::images::PixelFormat::NV12:
    case fuchsia::images::PixelFormat::YV12:
      error_reporter->ERROR() << "GpuImage::New(): PixelFormat must be BGRA_8 or R8G8B8A8.";
      return nullptr;
  }

  if (image_info.width <= 0) {
    error_reporter->ERROR() << "GpuImage::New(): width must be greater than 0.";
    return nullptr;
  }
  if (image_info.height <= 0) {
    error_reporter->ERROR() << "GpuImage::New(): height must be greater than 0.";
    return nullptr;
  }

  auto& caps = session->resource_context().vk_device_queues_capabilities;
  if (image_info.width > caps.max_image_width) {
    error_reporter->ERROR() << "GpuImage::New(): image width exceeds maximum (" << image_info.width
                            << " vs. " << caps.max_image_width << ").";
    return nullptr;
  }
  if (image_info.height > caps.max_image_height) {
    error_reporter->ERROR() << "GpuImage::New(): image height exceeds maximum ("
                            << image_info.height << " vs. " << caps.max_image_height << ").";
    return nullptr;
  }

  // TODO(fxbug.dev/47918): Support non-premultiplied alpha format and remove this.
  if (image_info.alpha_format == fuchsia::images::AlphaFormat::NON_PREMULTIPLIED) {
    error_reporter->ERROR() << "GpuImage::New(): Non-premultiplied alpha format "
                            << "is not supported yet.";
    return nullptr;
  }

  escher::ImageInfo escher_image_info;
  escher_image_info.format = pixel_format;
  escher_image_info.width = image_info.width;
  escher_image_info.height = image_info.height;
  escher_image_info.sample_count = 1;
  // If this image is shared cross-process these flags (and all other
  // vkCreateImage parameters) need to match those in the other process.
  // Other locations that need to match: topaz/flutter_runner/vulkan_surface.cc
  escher_image_info.usage =
      vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eColorAttachment;
  escher_image_info.is_external = true;
  escher_image_info.is_mutable = is_mutable;
  // TODO(SCN-1182): Add unit tests to verify this logic.
  switch (image_info.tiling) {
    case fuchsia::images::Tiling::LINEAR:
      escher_image_info.tiling = vk::ImageTiling::eLinear;
      break;
    case fuchsia::images::Tiling::GPU_OPTIMAL:
      escher_image_info.tiling = vk::ImageTiling::eOptimal;
      break;
  }
  // TODO(SCN-1012): Don't hardcode this -- use the data on the memory
  // object once we support a bitmask instead of an enum.
  escher_image_info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;

  constexpr auto kInitialLayout = vk::ImageLayout::ePreinitialized;
  vk::Device vk_device = session->resource_context().vk_device;
  vk::Image vk_image =
      escher::image_utils::CreateVkImage(vk_device, escher_image_info, kInitialLayout);

  // Make sure that the image is within range of its associated memory.
  vk::MemoryRequirements memory_reqs;
  vk_device.getImageMemoryRequirements(vk_image, &memory_reqs);

  if (memory_offset >= memory->size()) {
    error_reporter->ERROR() << "GpuImage::New(): the offset of the Image must be "
                            << "within the range of the Memory";
    return nullptr;
  }

  if (memory_offset + memory_reqs.size > memory->size()) {
    error_reporter->ERROR() << "GpuImage::New(): the Image must fit within the size "
                            << "of the Memory" << memory_reqs.size << " " << memory->size() << " "
                            << memory_offset;
    return nullptr;
  }

  // Make a pointer to a subregion of the memory, if necessary.
  escher::GpuMemPtr gpu_mem =
      (memory_offset > 0 || memory_reqs.size < memory->size())
          ? memory->GetGpuMem(error_reporter)->Suballocate(memory_reqs.size, memory_offset)
          : memory->GetGpuMem(error_reporter);

  escher::ImagePtr image = escher::impl::NaiveImage::AdoptVkImage(
      session->resource_context().escher_resource_recycler, escher_image_info, vk_image,
      std::move(gpu_mem), kInitialLayout);
  if (!image) {
    error_reporter->ERROR() << "GpuImage::New(): failed to adopt vk image";
    return nullptr;
  }
  return fxl::AdoptRef(new GpuImage(session, id, image));
}

GpuImagePtr GpuImage::New(Session* session, ResourceId id, MemoryPtr memory,
                          vk::ImageCreateInfo create_info, ErrorReporter* error_reporter) {
  auto vk_device = session->resource_context().vk_device;
  auto image_result = vk_device.createImage(create_info);
  if (image_result.result != vk::Result::eSuccess) {
    error_reporter->ERROR() << "GpuImage::New(): VkCreateImage failed: "
                            << vk::to_string(image_result.result);
    return nullptr;
  }

  // Make sure that the image is within range of its associated memory.
  vk::MemoryRequirements memory_reqs;
  vk_device.getImageMemoryRequirements(image_result.value, &memory_reqs);

  escher::GpuMemPtr gpu_mem = memory->GetGpuMem(error_reporter);
  FX_DCHECK(gpu_mem);

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

  escher::ImagePtr image = escher::impl::NaiveImage::AdoptVkImage(
      session->resource_context().escher_resource_recycler, image_info, image_result.value,
      std::move(gpu_mem), create_info.initialLayout);
  if (!image) {
    error_reporter->ERROR() << "GpuImage::New(): failed to adopt vk image";
    return nullptr;
  }
  return fxl::AdoptRef(new GpuImage(session, id, image));
}

void GpuImage::UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                                 escher::ImageLayoutUpdater* layout_updater) {
  FX_DCHECK(layout_updater) << "Layout updater doesn't exist!";
  if (!image_->is_layout_initialized()) {
    // TODO(fxbug.dev/36106): Currently we only convert the layout to |eShaderReadOnlyOptimal| --
    // this needs to be synchronized with topaz/runtime/flutter_runner.
    layout_updater->ScheduleSetImageInitialLayout(image_, vk::ImageLayout::eShaderReadOnlyOptimal);
  }
  dirty_ = UpdatePixels(gpu_uploader);
}

bool GpuImage::UpdatePixels(escher::BatchGpuUploader* uploader) { return false; }

}  // namespace gfx
}  // namespace scenic_impl
