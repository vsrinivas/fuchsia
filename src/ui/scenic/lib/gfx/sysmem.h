// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SYSMEM_H_
#define SRC_UI_SCENIC_LIB_GFX_SYSMEM_H_

#include <fuchsia/sysmem/cpp/fidl.h>

#include "src/lib/fxl/macros.h"

namespace scenic_impl {
namespace gfx {

// Wrapper class for the Sysmem Allocator service. Initializes and owns a connection to
// fuchsia::sysmem::Allocator and exposes methods for creating and importing buffer collections.
class Sysmem {
 public:
  Sysmem();
  ~Sysmem() = default;

  bool is_initialized() { return sysmem_allocator_.is_bound(); }

  fuchsia::sysmem::BufferCollectionTokenSyncPtr CreateBufferCollection();
  fuchsia::sysmem::BufferCollectionSyncPtr GetCollectionFromToken(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token);

 private:
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Sysmem);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SYSMEM_H_
