// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/util.h"

namespace a11y_manager {

zx_koid_t GetKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  return fsl::GetKoid(view_ref.reference.get());
}

}  // namespace a11y_manager
