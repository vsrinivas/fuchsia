// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "src/ui/examples/simplest_sysmem/sysmem_helper.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

namespace sysmem_helper {

BufferCollectionImportExportTokens BufferCollectionImportExportTokens::New() {
  BufferCollectionImportExportTokens ref_pair;
  zx_status_t status =
      zx::eventpair::create(0, &ref_pair.export_token.value, &ref_pair.import_token.value);
  FX_CHECK(status == ZX_OK);
  return ref_pair;
}

BufferCollectionConstraints CreateDefaultConstraints(BufferConstraint buffer_constraint) {
  fuchsia::sysmem::BufferCollectionConstraints constraints;
  constraints.has_buffer_memory_constraints = true;
  constraints.buffer_memory_constraints.cpu_domain_supported = true;
  constraints.buffer_memory_constraints.ram_domain_supported = true;
  constraints.usage.cpu = fuchsia::sysmem::cpuUsageReadOften | fuchsia::sysmem::cpuUsageWriteOften;
  constraints.min_buffer_count = buffer_constraint.buffer_count;

  constraints.image_format_constraints_count = 1;
  auto& image_constraints = constraints.image_format_constraints[0];
  image_constraints.color_spaces_count = 1;
  image_constraints.color_space[0] =
      fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::SRGB};
  image_constraints.pixel_format.type = buffer_constraint.pixel_format_type;
  image_constraints.pixel_format.has_format_modifier = true;
  image_constraints.pixel_format.format_modifier.value = fuchsia::sysmem::FORMAT_MODIFIER_LINEAR;

  image_constraints.required_min_coded_width = buffer_constraint.image_width;
  image_constraints.required_min_coded_height = buffer_constraint.image_height;
  image_constraints.required_max_coded_width = buffer_constraint.image_width;
  image_constraints.required_max_coded_height = buffer_constraint.image_height;
  image_constraints.bytes_per_row_divisor = buffer_constraint.bytes_per_pixel;

  return constraints;
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
}  // namespace sysmem_helper
