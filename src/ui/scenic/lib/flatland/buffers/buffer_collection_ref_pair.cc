// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/flatland/buffers/buffer_collection_ref_pair.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

namespace flatland {

BufferCollectionRefPair BufferCollectionRefPair::New() {
  BufferCollectionRefPair ref_pair;
  zx_status_t status =
      zx::eventpair::create(0, &ref_pair.export_token.value, &ref_pair.import_token.value);
  ZX_ASSERT(status == ZX_OK);
  return ref_pair;
}

fuchsia::ui::scenic::internal::BufferCollectionImportToken
BufferCollectionRefPair::DuplicateImportToken() {
  fuchsia::ui::scenic::internal::BufferCollectionImportToken import_dup;
  zx_status_t status = import_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_dup.value);
  ZX_ASSERT(status == ZX_OK);
  return import_dup;
}

}  // namespace flatland
