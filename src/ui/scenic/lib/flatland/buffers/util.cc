// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/buffers/util.h"

namespace flatland {

const fuchsia::sysmem::BufferUsage kNoneUsage = {.none = fuchsia::sysmem::noneUsage};

const std::pair<fuchsia::sysmem::BufferUsage, fuchsia::sysmem::BufferMemoryConstraints>
GetUsageAndMemoryConstraintsForCpuWriteOften() {
  const fuchsia::sysmem::BufferMemoryConstraints kCpuConstraints = {
      .ram_domain_supported = true,
      .cpu_domain_supported = true,
  };
  const fuchsia::sysmem::BufferUsage kCpuWriteUsage = {.cpu = fuchsia::sysmem::cpuUsageWriteOften};
  return std::make_pair(kCpuWriteUsage, kCpuConstraints);
}

void SetClientConstraintsAndWaitForAllocated(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count, uint32_t width,
    uint32_t height, fuchsia::sysmem::BufferUsage usage,
    const std::vector<uint64_t>& additional_format_modifiers,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  zx_status_t status =
      sysmem_allocator->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
  FX_DCHECK(status == ZX_OK);
  // Use a name with a priority thats > the vulkan implementation, but < what any client would use.
  buffer_collection->SetName(10u, "FlatlandImage");
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  if (memory_constraints) {
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = std::move(*memory_constraints);
  } else {
    constraints.has_buffer_memory_constraints = false;
  }
  constraints.usage = usage;
  constraints.min_buffer_count = image_count;

  constraints.image_format_constraints_count = 1 + additional_format_modifiers.size();
  for (size_t i = 0; i < constraints.image_format_constraints_count; i++) {
    auto& image_constraints = constraints.image_format_constraints[i];
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0] =
        fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::BGRA32;
    image_constraints.pixel_format.has_format_modifier = true;
    image_constraints.pixel_format.format_modifier.value =
        i == 0 ? fuchsia::sysmem::FORMAT_MODIFIER_LINEAR : additional_format_modifiers[i - 1];

    image_constraints.required_min_coded_width = width;
    image_constraints.required_min_coded_height = height;
    image_constraints.required_max_coded_width = width;
    image_constraints.required_max_coded_height = height;
    image_constraints.max_coded_width = width * 4 /*num channels*/;
    image_constraints.max_coded_height = height;
    image_constraints.max_bytes_per_row = 0xffffffff;
  }

  status = buffer_collection->SetConstraints(true, constraints);
  FX_DCHECK(status == ZX_OK);

  // Have the client wait for allocation.
  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info = {};
  status = buffer_collection->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  FX_DCHECK(status == ZX_OK);
  FX_DCHECK(allocation_status == ZX_OK);

  status = buffer_collection->Close();
  FX_DCHECK(status == ZX_OK);
}

fuchsia::sysmem::BufferCollectionSyncPtr CreateBufferCollectionSyncPtrAndSetConstraints(
    fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token, uint32_t image_count, uint32_t width,
    uint32_t height, fuchsia::sysmem::BufferUsage usage, fuchsia::sysmem::PixelFormatType format,
    std::optional<fuchsia::sysmem::BufferMemoryConstraints> memory_constraints) {
  fuchsia::sysmem::BufferCollectionSyncPtr buffer_collection;
  zx_status_t status =
      sysmem_allocator->BindSharedCollection(std::move(token), buffer_collection.NewRequest());
  FX_DCHECK(status == ZX_OK);
  // Use a name with a priority thats > the vulkan implementation, but < what any client would use.
  buffer_collection->SetName(10u, "FlatlandClientPointer");
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  if (memory_constraints) {
    constraints.has_buffer_memory_constraints = true;
    constraints.buffer_memory_constraints = std::move(*memory_constraints);
  } else {
    constraints.has_buffer_memory_constraints = false;
  }
  constraints.usage = usage;
  constraints.min_buffer_count = image_count;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];

  image_constraints.pixel_format.type = format;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;
  image_constraints.color_spaces_count = 1;
  switch (format) {
    case fuchsia::sysmem::PixelFormatType::BGRA32:
    case fuchsia::sysmem::PixelFormatType::R8G8B8A8:
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::SRGB;
      break;
    case fuchsia::sysmem::PixelFormatType::I420:
    case fuchsia::sysmem::PixelFormatType::NV12:
      image_constraints.color_space[0].type = fuchsia::sysmem::ColorSpaceType::REC709;
      break;
    default:
      FX_NOTREACHED();
  }

  image_constraints.required_min_coded_width = width;
  image_constraints.required_min_coded_height = height;
  image_constraints.required_max_coded_width = width;
  image_constraints.required_max_coded_height = height;
  image_constraints.max_coded_width = width * 4;
  image_constraints.max_coded_height = height;
  image_constraints.max_bytes_per_row = 0xffffffff;

  status = buffer_collection->SetConstraints(true, constraints);
  FX_DCHECK(status == ZX_OK);

  return buffer_collection;
}

void MapHostPointer(const fuchsia::sysmem::BufferCollectionInfo_2& collection_info,
                    uint32_t vmo_idx, std::function<void(uint8_t*, uint32_t)> callback) {
  // If the vmo idx is out of bounds pass in a nullptr and 0 bytes back to the caller.
  if (vmo_idx >= collection_info.buffer_count) {
    callback(nullptr, 0);
    return;
  }

  const zx::vmo& vmo = collection_info.buffers[vmo_idx].vmo;
  auto vmo_bytes = collection_info.settings.buffer_settings.size_bytes;
  FX_DCHECK(vmo_bytes > 0);

  uint8_t* vmo_host = nullptr;
  auto status = zx::vmar::root_self()->map(ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, /*vmar_offset*/ 0,
                                           vmo, /*vmo_offset*/ 0, vmo_bytes,
                                           reinterpret_cast<uintptr_t*>(&vmo_host));
  FX_DCHECK(status == ZX_OK);
  callback(vmo_host, vmo_bytes);

  // Unmap the pointer.
  uintptr_t address = reinterpret_cast<uintptr_t>(vmo_host);
  status = zx::vmar::root_self()->unmap(address, vmo_bytes);
  FX_DCHECK(status == ZX_OK);
}

}  // namespace flatland
