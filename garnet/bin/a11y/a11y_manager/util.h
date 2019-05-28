// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_
#define GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/fsl/handles/object_info.h>
#include <lib/zx/event.h>

namespace a11y_manager {

// Utility function to extract Koid from an event.
zx_koid_t GetKoid(const fuchsia::ui::views::ViewRef& view_ref);

// Multiply two 3x3 Matrix represented in Row Major form.
std::array<float, 9> Multiply3x3MatrixRowMajor(std::array<float, 9> left,
                                               std::array<float, 9> right);

}  // namespace a11y_manager

#endif  // GARNET_BIN_A11Y_A11Y_MANAGER_UTIL_H_
