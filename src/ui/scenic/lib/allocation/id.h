// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_ALLOCATION_ID_H_
#define SRC_UI_SCENIC_LIB_ALLOCATION_ID_H_

#include <zircon/types.h>

#include <cstdint>

namespace allocation {

using GlobalBufferCollectionId = zx_koid_t;
using GlobalImageId = uint64_t;

// Used to indicate an invalid buffer collection or image.
extern const GlobalBufferCollectionId kInvalidId;
extern const GlobalImageId kInvalidImageId;

// Atomically produces a new id that can be used to reference a buffer collection.
GlobalBufferCollectionId GenerateUniqueBufferCollectionId();

// Atomically produce a new id that can be used to reference a buffer collection's image.
GlobalImageId GenerateUniqueImageId();

}  // namespace allocation

#endif  // SRC_UI_SCENIC_LIB_ALLOCATION_ID_H_
