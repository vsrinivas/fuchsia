// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

namespace a11y {

// TODO(fxb/55220): Refine definition of describability.
bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_
