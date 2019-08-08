// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_VIEW_ID_H_
#define GARNET_LIB_UI_INPUT_VIEW_ID_H_

#include <ostream>
#include <vector>

#include "garnet/lib/ui/gfx/id.h"
#include "garnet/lib/ui/scenic/event_reporter.h"
#include "src/ui/lib/escher/geometry/types.h"

namespace scenic_impl {
namespace input {

// A ViewStack represents a stack of API endpoints that can receive focus, attachment, and input
// events. The top level endpoint is index 0, and grows downward.
struct ViewStack {
  struct Entry {
    // The session ID associated with this endpoint.
    SessionId session_id = 0;
    // The generic interface to send events to this endpoint. If the endpoint dies (either due to
    // the client closing it or due to the server responding to an error) this pointer should go out
    // of scope.
    EventReporterWeakPtr reporter;
    // The model-to-global transform for the UX element associated with this endpoint.
    glm::mat4 global_transform;
  };
  std::vector<Entry> stack;

  // Whether the top-level endpoint is focusable or not.
  // We write this field in an ADD event and read it in a DOWN event.
  bool focus_change = true;
};

std::ostream& operator<<(std::ostream& os, const ViewStack::Entry& value);
std::ostream& operator<<(std::ostream& os, const ViewStack& value);

}  // namespace input
}  // namespace scenic_impl
#endif  // GARNET_LIB_UI_INPUT_VIEW_ID_H_
