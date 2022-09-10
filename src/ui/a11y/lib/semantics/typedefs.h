// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_
#define SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_

#include "fuchsia/accessibility/semantics/cpp/fidl.h"

namespace a11y {
using NodeFilter = fit::function<bool(const fuchsia::accessibility::semantics::Node*)>;

// Same as NodeFilter, but takes a pointer to the node's parent along with the node.
// This saves us a costly call to 'GetParentNode'.
// TODO(fxbug.dev/108397) Cache parent nodes so that 'GetParentNode' is less costly.
using NodeFilterWithParent =
    fit::function<bool(const fuchsia::accessibility::semantics::Node* node,
                       const fuchsia::accessibility::semantics::Node* parent)>;

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SEMANTICS_TYPEDEFS_H_
