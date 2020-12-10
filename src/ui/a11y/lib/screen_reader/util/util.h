// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_
#define SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_

#include <fuchsia/accessibility/semantics/cpp/fidl.h>

#include <set>

#include "src/ui/a11y/lib/semantics/semantics_source.h"

namespace a11y {

// TODO(fxbug.dev/55220): Refine definition of describability.
bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node);

// Converts floating point to a string and strips trailing zeros.
std::string FormatFloat(float input);

// Get the set of nodes to exclude when traversing the tree from a particular
// node.
std::set<uint32_t> GetNodesToExclude(zx_koid_t koid, uint32_t node_id,
                                     SemanticsSource* semantics_source);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_
