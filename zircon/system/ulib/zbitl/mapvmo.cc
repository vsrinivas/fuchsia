// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/vmo.h>
#include <lib/zircon-internal/align.h>

namespace zbitl {

fitx::result<zx_status_t, ByteView> StorageTraits<MapUnownedVmo>::DoRead(MapUnownedVmo& zbi,
                                                                         uint64_t payload,
                                                                         uint32_t length) {
  if (length == 0) {
    return fitx::ok(ByteView{});
  }

  auto mapped = [&zbi](uint64_t offset_in_mapping, uint32_t length) {
    return fitx::ok(zbi.mapping_.bytes().substr(offset_in_mapping, length));
  };

  // Check if the current mapping already covers it.
  if (payload >= zbi.mapping_.offset_) {
    const uint64_t offset_in_mapping = payload - zbi.mapping_.offset_;
    if (offset_in_mapping < zbi.mapping_.size_ &&
        zbi.mapping_.size_ - offset_in_mapping >= length) {
      return mapped(offset_in_mapping, length);
    }
  }

  zbi.mapping_ = MapUnownedVmo::Mapping{};  // Unmap any cached mapping.

  // Mapping must take place along page boundaries.
  const uint64_t previous_page_boundary = payload & -ZX_PAGE_SIZE;
  const uint64_t next_page_boundary = ZX_PAGE_ALIGN(payload + length);
  const size_t size = next_page_boundary - previous_page_boundary;
  const uint64_t offset_in_mapping = payload % ZX_PAGE_SIZE;

  if (zx_status_t status = zbi.vmar().map(0, zbi.vmo(), previous_page_boundary, size,
                                          ZX_VM_PERM_READ, &zbi.mapping_.address_);
      status != ZX_OK) {
    return fitx::error{status};
  }
  zbi.mapping_.offset_ = previous_page_boundary;
  zbi.mapping_.size_ = size;

  return mapped(offset_in_mapping, length);
}

MapUnownedVmo::~MapUnownedVmo() {
  if (mapping_.size_ != 0) {
    vmar_->unmap(mapping_.address_, mapping_.size_);
  }
}

}  // namespace zbitl
