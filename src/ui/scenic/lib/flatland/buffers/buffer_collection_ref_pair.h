// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_BUFFER_COLLECTION_REF_PAIR_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_BUFFER_COLLECTION_REF_PAIR_H_

#include <fuchsia/ui/scenic/internal/cpp/fidl.h>
#include <lib/zx/eventpair.h>

namespace flatland {

// Convenience function which allows clients to easily create a valid |BufferCollectionExportToken|
// / |BufferCollectionImportToken| pair for use between Allocator and Flatland.
struct BufferCollectionRefPair {
  static BufferCollectionRefPair New();
  fuchsia::ui::scenic::internal::BufferCollectionImportToken DuplicateImportToken();

  fuchsia::ui::scenic::internal::BufferCollectionExportToken export_token;
  fuchsia::ui::scenic::internal::BufferCollectionImportToken import_token;
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_BUFFERS_BUFFER_COLLECTION_REF_PAIR_H_
