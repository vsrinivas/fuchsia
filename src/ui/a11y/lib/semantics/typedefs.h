// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_

#include "fuchsia/accessibility/semantics/cpp/fidl.h"

namespace a11y {
using NodeFilter = fit::function<bool(const fuchsia::accessibility::semantics::Node*)>;
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_
