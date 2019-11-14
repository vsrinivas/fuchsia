// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_VIEW_STACK_H_
#define SRC_UI_SCENIC_LIB_INPUT_VIEW_STACK_H_

#include <zircon/types.h>

#include <ostream>
#include <vector>

#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/gfx/id.h"
#include "src/ui/scenic/lib/scenic/event_reporter.h"

namespace scenic_impl::input {

// A ViewStack represents a stack of API endpoints that can receive focus, attachment, and input
// events. The top level endpoint is index 0, and grows downward.
struct ViewStack {
  struct Entry {
    // The ViewRef's KOID associated with this endpoint.
    zx_koid_t view_ref_koid = ZX_KOID_INVALID;
    // The generic interface to send events to this endpoint. If the endpoint dies (either due to
    // the client closing it or due to the server responding to an error) this pointer should go out
    // of scope.
    EventReporterWeakPtr reporter;
    // The transform from input device coordinates to the local coordinate space of the UX element
    // associated with this endpoint. This, as opposed to hit testing every time, allows us to latch
    // move events to an element that was hit on down (in addition to saving on the hit test).
    glm::mat4 transform = glm::mat4(1.f);
  };

  std::vector<Entry> stack;
};

std::ostream& operator<<(std::ostream& os, const ViewStack::Entry& value);
std::ostream& operator<<(std::ostream& os, const ViewStack& value);

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_VIEW_STACK_H_
