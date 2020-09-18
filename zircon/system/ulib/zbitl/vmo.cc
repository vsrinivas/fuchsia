// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zbitl/vmo.h>

namespace zbitl {

fitx::result<zx_status_t, uint32_t> StorageTraits<zx::vmo>::Capacity(const zx::vmo& vmo) {
  uint64_t vmo_size;
  zx_status_t status = vmo.get_size(&vmo_size);
  if (status == ZX_OK) {
    uint64_t content_size;
    status = vmo.get_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
    if (status == ZX_OK && content_size != 0) {
      vmo_size = content_size;
    }
  }
  if (status != ZX_OK) {
    return fitx::error{status};
  }
  return fitx::ok(static_cast<uint32_t>(
      std::min(static_cast<uint64_t>(std::numeric_limits<uint32_t>::max()), vmo_size)));
}

fitx::result<zx_status_t, zbi_header_t> StorageTraits<zx::vmo>::Header(const zx::vmo& vmo,
                                                                       uint32_t offset) {
  zbi_header_t header;
  zx_status_t status = vmo.read(&header, offset, sizeof(header));
  if (status != ZX_OK) {
    return fitx::error{status};
  }
  return fitx::ok(header);
}

fitx::result<zx_status_t> StorageTraits<zx::vmo>::Write(const zx::vmo& vmo, uint32_t offset,
                                                        ByteView data) {
  zx_status_t status = vmo.write(data.data(), offset, data.size());
  if (status != ZX_OK) {
    return fitx::error{status};
  }
  return fitx::ok();
}

fitx::result<zx_status_t, zx::vmo> StorageTraits<zx::vmo>::Create(const zx::vmo&, size_t size) {
  zx::vmo vmo;
  zx_status_t status = zx::vmo::create(size, ZX_VMO_RESIZABLE, &vmo);
  if (status == ZX_OK) {
    status = vmo.set_property(ZX_PROP_VMO_CONTENT_SIZE, &size, sizeof(size));
  }
  if (status != ZX_OK) {
    return fitx::error{status};
  }
  return fitx::ok(std::move(vmo));
}

fitx::result<zx_status_t, std::optional<std::pair<zx::vmo, uint32_t>>>
StorageTraits<zx::vmo>::DoClone(const zx::vmo& original, uint32_t offset, uint32_t length) {
  const uint32_t slop = offset % uint32_t{ZX_PAGE_SIZE};
  const uint32_t clone_start = offset & -uint32_t{ZX_PAGE_SIZE};
  const uint32_t clone_size = slop + length;

  zx::vmo clone;
  zx_status_t status = original.create_child(ZX_VMO_CHILD_SNAPSHOT | ZX_VMO_CHILD_RESIZABLE,
                                             clone_start, clone_size, &clone);
  if (status == ZX_OK && slop > 0) {
    // Explicitly zero the partial page before the range so it remains unseen.
    status = clone.op_range(ZX_VMO_OP_ZERO, 0, slop, nullptr, 0);
  }
  if (status == ZX_OK && clone_size % ZX_PAGE_SIZE != 0) {
    // Explicitly zero the partial page after the range so it remains unseen.
    status = clone.op_range(ZX_VMO_OP_ZERO, clone_size, ZX_PAGE_SIZE - (clone_size % ZX_PAGE_SIZE),
                            nullptr, 0);
  }
  if (status == ZX_OK) {
    const uint64_t content_size = slop + length;
    status = clone.set_property(ZX_PROP_VMO_CONTENT_SIZE, &content_size, sizeof(content_size));
  }

  if (status != ZX_OK) {
    return fitx::error{status};
  }

  return fitx::ok(std::make_pair(std::move(clone), slop));
}

}  // namespace zbitl
