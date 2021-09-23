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

// Returns true if the node represents a slider.
bool NodeIsSlider(const fuchsia::accessibility::semantics::Node& node);

// Get the string representation of a slider's value. Some sliders use the
// range_value field to store a float value, while others use the value field
// to store a string representation. We prefer range_value, but if it's not
// present, we fall back to value.
std::string GetSliderValue(const fuchsia::accessibility::semantics::Node& node);

}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_SCREEN_READER_UTIL_UTIL_H_
