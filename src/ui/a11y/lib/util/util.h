// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_UTIL_UTIL_H_
#define SRC_UI_A11Y_LIB_UTIL_UTIL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/zx/event.h>

#include "src/lib/fsl/handles/object_info.h"

namespace a11y {

// Utility function to extract Koid from a View Ref.
zx_koid_t GetKoid(const fuchsia::ui::views::ViewRef& view_ref);

// Multiply two 3x3 Matrix represented in Row Major form.
std::array<float, 9> Multiply3x3MatrixRowMajor(std::array<float, 9> left,
                                               std::array<float, 9> right);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_UTIL_UTIL_H_
