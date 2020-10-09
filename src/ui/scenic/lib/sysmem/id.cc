// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/sysmem/id.h"

#include <atomic>

namespace sysmem_util {

const GlobalBufferCollectionId kInvalidId = 0;

GlobalBufferCollectionId GenerateUniqueBufferCollectionId() {
  // This function will be called from multiple threads, and thus needs an atomic
  // incrementor for the id.
  static std::atomic<GlobalBufferCollectionId> buffer_collection_id = 0;
  return ++buffer_collection_id;
}

}  // namespace sysmem_util
