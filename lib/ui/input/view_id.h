// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_VIEW_ID_H_
#define GARNET_LIB_UI_INPUT_VIEW_ID_H_

#include <ostream>
#include <vector>

#include "garnet/lib/ui/gfx/id.h"

namespace scenic {
namespace input {

// A View is a Resource in a Session. Need both to uniquely identify.
struct ViewId {
  ViewId() : session_id(0), resource_id(0) {}
  ViewId(scenic::SessionId s, scenic::ResourceId r)
      : session_id(s), resource_id(r) {}

  explicit operator bool();

  scenic::SessionId session_id;
  scenic::ResourceId resource_id;
};

// The top-level View is index 0, and grows downward.
struct ViewStack {
  std::vector<ViewId> stack;
};

bool operator==(const ViewId& lhs, const ViewId& rhs);
bool operator!=(const ViewId& lhs, const ViewId& rhs);

std::ostream& operator<<(std::ostream& os, const ViewId& value);
std::ostream& operator<<(std::ostream& os, const ViewStack& value);

}  // namespace input
}  // namespace scenic
#endif  // GARNET_LIB_UI_INPUT_VIEW_ID_H_
