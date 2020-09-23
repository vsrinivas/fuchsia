// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_DISPLAY_UTIL_H_
#define SRC_UI_SCENIC_LIB_DISPLAY_UTIL_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>

#include <cstdint>

namespace scenic_impl {
using DisplayBufferCollectionId = uint64_t;
using DisplayEventId = uint64_t;

// Atomically produces a new id that can be used to reference a buffer collection.
DisplayBufferCollectionId GenerateUniqueCollectionId();

// Imports a sysmem buffer collection token to a display controller, and sets the constraints.
// A successful import will return true, otherwise it will return false.
bool ImportBufferCollection(DisplayBufferCollectionId identifier,
                            const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                            fuchsia::sysmem::BufferCollectionTokenSyncPtr token,
                            const fuchsia::hardware::display::ImageConfig& image_config);

// Imports a zx::event to the provided display controller. The return value is an ID to
// reference that event on other display controller functions that take an event as an
// argument. On failure, the return value will be fuchsia::hardware::display::INVALID_DISP_ID.
DisplayEventId ImportEvent(const fuchsia::hardware::display::ControllerSyncPtr& display_controller,
                           const zx::event& event);

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_DISPLAY_UTIL_H_
