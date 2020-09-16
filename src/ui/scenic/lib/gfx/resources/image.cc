// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image.h"

#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"
#include "src/ui/scenic/lib/gfx/resources/host_image.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Image::kTypeInfo = {ResourceType::kImage | ResourceType::kImageBase,
                                           "Image"};

Image::Image(Session* session, ResourceId id, const ResourceTypeInfo& type_info)
    : ImageBase(session, id, type_info) {
  FX_DCHECK(type_info.IsKindOf(Image::kTypeInfo));
}

ImagePtr Image::New(Session* session, ResourceId id, MemoryPtr memory,
                    const fuchsia::images::ImageInfo& image_info, uint64_t memory_offset,
                    ErrorReporter* error_reporter) {
  // Create from host memory.
  if (memory->is_host()) {
    return HostImage::New(session, id, memory, image_info, memory_offset, error_reporter);

    // Create from GPU memory.
  } else {
    return GpuImage::New(session, id, memory, image_info, memory_offset, error_reporter);
  }
}

ImagePtr Image::New(Session* session, ResourceId id, uint32_t width, uint32_t height,
                    uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                    ErrorReporter* error_reporter) {
  auto buffer_collection_it = session->BufferCollections().find(buffer_collection_id);
  if (buffer_collection_it == session->BufferCollections().end()) {
    FX_LOGS(ERROR) << "buffer_collection_id " << buffer_collection_id
                   << " has not yet been registered.";
    return nullptr;
  }

  BufferCollectionInfo& info = buffer_collection_it->second;

  if (!info.BuffersAreAllocated()) {
    FX_LOGS(ERROR) << "Failed to wait for buffer allocation.";
    return nullptr;
  }

  auto result = info.GetVMO(buffer_collection_index);
  if (result.is_error()) {
    return nullptr;
  }

  zx::vmo vmo = std::move(result.value());

  auto vk_device = session->resource_context().vk_device;
  FX_DCHECK(vk_device);
  auto vk_loader = session->resource_context().vk_loader;
  auto collection_properties =
      vk_device.getBufferCollectionPropertiesFUCHSIA(info.GetFuchsiaCollection(), vk_loader);
  if (collection_properties.result != vk::Result::eSuccess) {
    FX_LOGS(ERROR) << "Failed to get buffer collection properties.";
    return nullptr;
  }

  const uint32_t memory_type_index =
      escher::CountTrailingZeros(collection_properties.value.memoryTypeBits);
  vk::ImportMemoryBufferCollectionFUCHSIA import_info;
  import_info.collection = info.GetFuchsiaCollection();
  import_info.index = buffer_collection_index;
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.setPNext(&import_info);
  alloc_info.memoryTypeIndex = memory_type_index;
  MemoryPtr memory = Memory::New(session, 0u, std::move(vmo), alloc_info, error_reporter);
  if (!memory) {
    FX_LOGS(ERROR) << "Failed to create Memory resource.";
    return nullptr;
  }

  vk::Format pixel_format = escher::SysmemPixelFormatTypeToVkFormat(
      info.GetSysmemInfo().settings.image_format_constraints.pixel_format.type);
  if (pixel_format == vk::Format::eUndefined) {
    FX_LOGS(ERROR) << "Pixel format not supported.";
    return nullptr;
  }

  vk::BufferCollectionImageCreateInfoFUCHSIA collection_image_info;
  collection_image_info.collection = info.GetFuchsiaCollection();
  collection_image_info.index = buffer_collection_index;
  vk::ImageCreateInfo image_create_info =
      escher::image_utils::GetDefaultImageConstraints(pixel_format);
  image_create_info.setPNext(&collection_image_info);
  image_create_info.extent = vk::Extent3D{width, height, 1};
  if (info.GetSysmemInfo().settings.buffer_settings.is_secure) {
    image_create_info.flags = vk::ImageCreateFlagBits::eProtected;
  }

  info.ImageResourceIds().insert(id);

  // Create GpuImage object since Vulkan constraints set on BufferCollection guarantee that it will
  // be device memory.
  return GpuImage::New(session, id, memory, image_create_info, error_reporter);
}

void Image::UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                              escher::ImageLayoutUpdater* layout_updater) {
  if (dirty_) {
    dirty_ = UpdatePixels(gpu_uploader);
  }
}

const escher::ImagePtr& Image::GetEscherImage() {
  static const escher::ImagePtr kNullEscherImage;
  return dirty_ ? kNullEscherImage : image_;
}

}  // namespace gfx
}  // namespace scenic_impl
