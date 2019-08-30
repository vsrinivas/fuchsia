// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_FORWARD_DECLARATIONS_H_
#define SRC_UI_SCENIC_LIB_SCENIC_FORWARD_DECLARATIONS_H_

#include <cstdint>

namespace scenic_impl {

class Clock;
class CommandDispatcher;
class Scenic;
class Session;

using SessionId = uint64_t;

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_FORWARD_DECLARATIONS_H_
