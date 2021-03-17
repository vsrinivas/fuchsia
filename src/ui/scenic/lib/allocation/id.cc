// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/allocation/id.h"

#include <atomic>

namespace allocation {

const GlobalBufferCollectionId kInvalidId = ZX_KOID_INVALID;
const GlobalImageId kInvalidImageId = 0;

GlobalBufferCollectionId GenerateUniqueBufferCollectionId() {
  // This function will be called from multiple threads, and thus needs an atomic
  // incrementor for the id.
  static std::atomic<GlobalBufferCollectionId> buffer_collection_id = 0;
  return ++buffer_collection_id;
}

GlobalImageId GenerateUniqueImageId() {
  // This function will be called from multiple threads, and thus needs an atomic
  // incrementor for the id.
  static std::atomic<GlobalImageId> image_id = 0;
  return ++image_id;
}
}  // namespace allocation
