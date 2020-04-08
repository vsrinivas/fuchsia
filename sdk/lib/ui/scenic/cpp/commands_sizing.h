// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_COMMANDS_SIZING_H_
#define LIB_UI_SCENIC_CPP_COMMANDS_SIZING_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>

namespace scenic {

struct CommandSize {
    explicit CommandSize(int64_t num_bytes, int64_t num_handles)
        : num_bytes(num_bytes), num_handles(num_handles) {}

    const int64_t num_bytes;
    const int64_t num_handles;
};

// Helper function to measure a Scenic command. The size returned is a safe
// upper-bound, rather than a precise size.
//
// For commands containaining arrays or vectors, the code falls back to maxing
// out both the number of bytes and number of handles thus safely
// over-evaluating their size.
CommandSize MeasureCommand(const fuchsia::ui::scenic::Command& command);

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_COMMANDS_SIZING_H_
