// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/util/util.h"

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

zx_koid_t GetKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  return GetHandleKoid(view_ref.reference.get());
}

zx_koid_t GetHandleKoid(const zx_handle_t& handle) { return fsl::GetKoid(handle); }

fuchsia::ui::views::ViewRef Clone(const fuchsia::ui::views::ViewRef& view_ref) {
  fuchsia::ui::views::ViewRef clone;
  FX_CHECK(fidl::Clone(view_ref, &clone) == ZX_OK);
  return clone;
}

std::array<float, 9> Multiply3x3MatrixRowMajor(std::array<float, 9> left,
                                               std::array<float, 9> right) {
  std::array<float, 9> result;
  for (int row = 0; row < 3; row++) {
    for (int column = 0; column < 3; column++) {
      result[3 * row + column] = left[3 * row] * right[column] +
                                 left[3 * row + 1] * right[column + 3] +
                                 left[3 * row + 2] * right[column + 6];
    }
  }
  return result;
}

}  // namespace a11y
