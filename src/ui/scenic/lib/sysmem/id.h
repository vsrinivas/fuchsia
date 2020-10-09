// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SYSMEM_ID_H_
#define SRC_UI_SCENIC_LIB_SYSMEM_ID_H_

#include <cstdint>

namespace sysmem_util {
using GlobalBufferCollectionId = uint64_t;

// Used to indicate an invalid buffer collection.
extern const GlobalBufferCollectionId kInvalidId;

// Atomically produces a new id that can be used to reference a buffer collection.
GlobalBufferCollectionId GenerateUniqueBufferCollectionId();

}  // namespace sysmem_util

#endif  // SRC_UI_SCENIC_LIB_SYSMEM_ID_H_
