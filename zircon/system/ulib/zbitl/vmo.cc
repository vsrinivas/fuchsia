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

}  // namespace zbitl
