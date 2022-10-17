// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/elfldltl/mapped-vmo-file.h"

#include <lib/zx/vmar.h>
#include <zircon/assert.h>
#include <zircon/status.h>

namespace elfldltl {

zx::result<> MappedVmoFile::Init(zx::unowned_vmo vmo, zx::unowned_vmar vmar) {
  uint64_t vmo_size;
  zx_status_t status = vmo->get_size(&vmo_size);
  if (status == ZX_OK) {
    uint64_t content_size = vmo_size;
    status = vmo->get_prop_content_size(&content_size);
    if (status == ZX_OK) {
      uintptr_t mapped;
      status = vmar->map(ZX_VM_PERM_READ, 0, *vmo, 0, vmo_size, &mapped);
      if (status == ZX_OK) {
        set_image({reinterpret_cast<std::byte*>(mapped), content_size});
        vmar_ = vmar->borrow();
        mapped_size_ = vmo_size;
      }
    }
  }
  if (status != ZX_OK) {
    return zx::error{status};
  }
  return zx::ok();
}

MappedVmoFile::~MappedVmoFile() {
  if (mapped_size_ != 0) {
    [[maybe_unused]] zx_status_t status =
        vmar_->unmap(reinterpret_cast<uintptr_t>(image().data()), mapped_size_);
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "unmap(%p, %#zx) -> %s", image().data(), mapped_size_,
                        zx_status_get_string(status));
  }
}

}  // namespace elfldltl
