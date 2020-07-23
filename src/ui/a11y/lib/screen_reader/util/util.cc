// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {

bool NodeIsDescribable(const fuchsia::accessibility::semantics::Node* node) {
  return node &&
         ((node->has_attributes() && node->attributes().has_label() &&
           !node->attributes().label().empty()) ||
          (node->has_role() && node->role() == fuchsia::accessibility::semantics::Role::BUTTON));
}

}  // namespace a11y
