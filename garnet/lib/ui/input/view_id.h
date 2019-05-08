// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_VIEW_ID_H_
#define GARNET_LIB_UI_INPUT_VIEW_ID_H_

#include <ostream>
#include <vector>

#include "garnet/lib/ui/gfx/id.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace scenic_impl {
namespace input {

// The top-level View is index 0, and grows downward.
struct ViewStack {
  struct Entry {
    // We store the View's resource ID to distinguish between Views vended by a
    // single Session. However, a View's RefPtr may not actually be in the
    // Session's ResourceMap, so the resource ID is *not* useful for recall.
    GlobalId view_id;
    glm::mat4 global_transform;  // The model-to-global transform for each View.
  };
  std::vector<Entry> stack;

  // Whether the top-level View is focusable or not.
  // We write this field in an ADD event and read it in a DOWN event.
  bool focus_change = true;
};

std::ostream& operator<<(std::ostream& os, const ViewStack& value);

}  // namespace input
}  // namespace scenic_impl
#endif  // GARNET_LIB_UI_INPUT_VIEW_ID_H_
