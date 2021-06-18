// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORT_EXPORT_TOKENS_H_
#define SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORT_EXPORT_TOKENS_H_

#include <fuchsia/scenic/allocation/cpp/fidl.h>
#include <lib/zx/eventpair.h>

namespace allocation {

using fuchsia::scenic::allocation::BufferCollectionExportToken;
using fuchsia::scenic::allocation::BufferCollectionImportToken;

// Convenience function which allows clients to easily create a valid |BufferCollectionExportToken|
// / |BufferCollectionImportToken| pair for use between Allocator and Flatland.
struct BufferCollectionImportExportTokens {
  static BufferCollectionImportExportTokens New();
  BufferCollectionImportToken DuplicateImportToken();

  BufferCollectionExportToken export_token;
  BufferCollectionImportToken import_token;
};

}  // namespace allocation

#endif  // SRC_UI_SCENIC_LIB_ALLOCATION_BUFFER_COLLECTION_IMPORT_EXPORT_TOKENS_H_
