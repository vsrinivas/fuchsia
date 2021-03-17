// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>

namespace allocation {

BufferCollectionImportExportTokens BufferCollectionImportExportTokens::New() {
  BufferCollectionImportExportTokens ref_pair;
  zx_status_t status =
      zx::eventpair::create(0, &ref_pair.export_token.value, &ref_pair.import_token.value);
  ZX_ASSERT(status == ZX_OK);
  return ref_pair;
}

BufferCollectionImportToken BufferCollectionImportExportTokens::DuplicateImportToken() {
  BufferCollectionImportToken import_dup;
  zx_status_t status = import_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_dup.value);
  ZX_ASSERT(status == ZX_OK);
  return import_dup;
}

}  // namespace allocation
