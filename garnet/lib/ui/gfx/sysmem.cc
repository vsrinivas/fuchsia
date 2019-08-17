// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/sysmem.h"

#include <lib/fdio/directory.h>

#include "src/lib/fxl/logging.h"

namespace scenic_impl {
namespace gfx {

Sysmem::Sysmem() {
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator_.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    sysmem_allocator_.Unbind();
    FXL_LOG(ERROR) << "Unable to connect to sysmem: " << status;
  }
}

fuchsia::sysmem::BufferCollectionTokenSyncPtr Sysmem::CreateBufferCollection() {
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  zx_status_t status = sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "CreateBufferCollection failed " << status;
    return nullptr;
  }
  return local_token;
}

fuchsia::sysmem::BufferCollectionSyncPtr Sysmem::GetCollectionFromToken(
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token) {
  fuchsia::sysmem::BufferCollectionSyncPtr collection;
  zx_status_t status =
      sysmem_allocator_->BindSharedCollection(std::move(token), collection.NewRequest());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "BindSharedCollection failed " << status;
    return nullptr;
  }
  return collection;
}

}  // namespace gfx
}  // namespace scenic_impl
