// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_COMMON_DISPLAY_UTIL_H_
#define SRC_UI_SCENIC_LIB_COMMON_DISPLAY_UTIL_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <cstdint>

namespace scenic {
using DisplayBufferCollectionId = uint64_t;

// Imports a sysmem buffer collection token to a display controller, and sets the constraints.
// A successful import will return a unique ID to reference the collection by. Failure will
// result in a return value of 0 for the ID, signifying an invalid collection.
DisplayBufferCollectionId ImportBufferCollection(
    const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
    fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
    const fuchsia::hardware::display::ImageConfig& image_config);

}  // namespace scenic

#endif  // SRC_UI_SCENIC_LIB_COMMON_DISPLAY_UTIL_H_
