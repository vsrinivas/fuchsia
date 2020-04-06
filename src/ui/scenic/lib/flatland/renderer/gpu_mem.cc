// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/renderer/gpu_mem.h"

#include "src/ui/lib/escher/util/bit_ops.h"

namespace {

escher::GpuMemPtr CreateGPUMem(const vk::Device& device, vk::MemoryAllocateInfo* alloc_info) {
  FXL_DCHECK(device);
  vk::DeviceMemory memory = nullptr;
  vk::Result err = device.allocateMemory(alloc_info, nullptr, &memory);
  if (err != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Could not successfully allocate memory.";
    return nullptr;
  }
  return escher::GpuMem::AdoptVkMemory(device, vk::DeviceMemory(memory), alloc_info->allocationSize,
                                       /*needs_mapped_ptr*/ false);
}

}  // anonymous namespace

namespace flatland {

vk::ImageCreateInfo GpuImageInfo::NewVkImageCreateInfo(uint32_t width, uint32_t height) const {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.extent = vk::Extent3D{width, height, 1};
  create_info.flags = vk::ImageCreateFlagBits::eMutableFormat;

  // Hardcode a single viable pixel format. We want to be able to test every single possible
  // format that is allowed into Flatland, and so we start with just one.
  create_info.format = vk::Format::eB8G8R8A8Unorm;
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;

  if (p_extension_) {
    create_info.setPNext(&(*p_extension_));
  }

  if (is_protected_) {
    create_info.flags = vk::ImageCreateFlagBits::eProtected;
  }
  return create_info;
}

GpuImageInfo GpuImageInfo::New(const vk::Device& device, const vk::DispatchLoaderDynamic& vk_loader,
                               const fuchsia::sysmem::BufferCollectionInfo_2& info,
                               const vk::BufferCollectionFUCHSIA& vk_buffer_collection,
                               uint32_t index) {
  FXL_DCHECK(device);
  FXL_DCHECK(vk_loader.vkGetBufferCollectionPropertiesFUCHSIA);

  // Check the provided index against actually allocated number of buffers.
  if (info.buffer_count <= index) {
    FXL_LOG(ERROR) << "Specified vmo index is out of bounds: " << index;
    return GpuImageInfo();
  }

  // Currently only support a single format.
  FXL_DCHECK(info.settings.image_format_constraints.pixel_format.type ==
             fuchsia::sysmem::PixelFormatType::BGRA32);

  // Get a handle to the vmo and extract the size of its buffer.
  zx::vmo vmo;
  zx_status_t status = info.buffers[index].vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
  FXL_DCHECK(status == ZX_OK);
  uint64_t vmo_size;
  status = vmo.get_size(&vmo_size);
  FXL_DCHECK(status == ZX_OK);

  auto collection_properties =
      device.getBufferCollectionPropertiesFUCHSIA(vk_buffer_collection, vk_loader);
  if (collection_properties.result != vk::Result::eSuccess) {
    FXL_LOG(ERROR) << "Could not get collection properties for vk_buffer_collection.h";
    return GpuImageInfo();
  }

  // We are setting up the information here to import the buffer collection vmo at the
  // specified index into GPU memory.
  const uint32_t memory_type_index =
      escher::CountTrailingZeros(collection_properties.value.memoryTypeBits);
  vk::ImportMemoryBufferCollectionFUCHSIA import_info;
  import_info.collection = vk_buffer_collection;
  import_info.index = index;
  vk::MemoryAllocateInfo alloc_info;
  alloc_info.setPNext(&import_info);
  alloc_info.memoryTypeIndex = memory_type_index;
  alloc_info.allocationSize = vmo_size;

  return GpuImageInfo(CreateGPUMem(device, &alloc_info), vk_buffer_collection, index,
                      info.settings.buffer_settings.is_secure);
}

GpuImageInfo::GpuImageInfo(escher::GpuMemPtr mem, vk::BufferCollectionFUCHSIA vk_buffer_collection,
                           uint32_t vmo_index, bool is_protected)
    : mem_(mem), is_protected_(is_protected) {
  vk::BufferCollectionImageCreateInfoFUCHSIA p_extension;
  p_extension.collection = vk_buffer_collection;
  p_extension.index = vmo_index;
  p_extension_ = p_extension;
}

}  // namespace flatland
