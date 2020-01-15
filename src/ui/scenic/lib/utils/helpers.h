// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
#define SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/zx/event.h>

namespace utils {

// Copy a zx::event.
zx::event CopyEvent(const zx::event& event);

// Helper for creating a Present2Args fidl struct.
fuchsia::ui::scenic::Present2Args CreatePresent2Args(zx_time_t requested_presentation_time,
                                                     std::vector<zx::event> acquire_fences,
                                                     std::vector<zx::event> release_fences,
                                                     zx_duration_t requested_prediction_span);

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_HELPERS_H_
