// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/vmo.h>
#include <lib/zircon-internal/align.h>

namespace zbitl {

fit::result<zx_status_t, void*> StorageTraits<MapUnownedVmo>::Map(MapUnownedVmo& zbi,
                                                                  uint64_t payload, uint32_t length,
                                                                  bool write) {
  ZX_ASSERT_MSG(!write || zbi.writable_, "map-VMO not configured to be written to");

  if (length == 0) {
    return fit::ok(nullptr);
  }

  auto mapped = [&zbi](uint64_t offset_in_mapping, uint32_t length) {
    std::byte* data = zbi.mapping_.data();
    return fit::ok(data + static_cast<size_t>(offset_in_mapping));
  };

  // Check if the current mapping already covers it.
  if (payload >= zbi.mapping_.offset_) {
    const uint64_t offset_in_mapping = payload - zbi.mapping_.offset_;
    if (offset_in_mapping < zbi.mapping_.size_ &&
        zbi.mapping_.size_ - offset_in_mapping >= length) {
      if (write && !zbi.mapping_.write_) {
        zx_status_t status =
            zbi.vmar().protect(ZX_VM_PERM_READ | (zbi.writable_ ? ZX_VM_PERM_WRITE : 0),
                               zbi.mapping_.address_, zbi.mapping_.size_);
        if (status != ZX_OK) {
          return fit::error{status};
        }
        zbi.mapping_.write_ = true;
      }
      return mapped(offset_in_mapping, length);
    }
  }

  zbi.mapping_ = MapUnownedVmo::Mapping{};  // Unmap any cached mapping.

  // Mapping must take place along page boundaries.
  const uint64_t previous_page_boundary = payload & -ZX_PAGE_SIZE;
  const uint64_t next_page_boundary = ZX_PAGE_ALIGN(payload + length);
  const size_t size = next_page_boundary - previous_page_boundary;
  const uint64_t offset_in_mapping = payload % ZX_PAGE_SIZE;
  if (zx_status_t status =
          zbi.vmar().map(ZX_VM_PERM_READ | (zbi.writable_ ? ZX_VM_PERM_WRITE : 0), 0, zbi.vmo(),
                         previous_page_boundary, size, &zbi.mapping_.address_);
      status != ZX_OK) {
    return fit::error{status};
  }
  zbi.mapping_.offset_ = previous_page_boundary;
  zbi.mapping_.size_ = size;
  zbi.mapping_.write_ = write;

  return mapped(offset_in_mapping, length);
}

MapUnownedVmo::~MapUnownedVmo() {
  if (mapping_.size_ != 0) {
    vmar_->unmap(mapping_.address_, mapping_.size_);
  }
}

}  // namespace zbitl
