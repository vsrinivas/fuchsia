// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_INPUT_FOCUS_H_
#define GARNET_LIB_UI_INPUT_FOCUS_H_

#include <vector>

#include "garnet/lib/ui/gfx/id.h"

namespace scenic {
namespace input {

// A View is a Resource in a Session. Need both to uniquely identify.
struct ViewId {
  scenic::SessionId session_id;
  scenic::ResourceId resource_id;
};

struct FocusChain {
  std::vector<ViewId> chain;
};

}  // namespace input
}  // namespace scenic
#endif  // GARNET_LIB_UI_INPUT_FOCUS_H_
