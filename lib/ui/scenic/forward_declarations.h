// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_SCENIC_FORWARD_DECLARATIONS_H_
#define GARNET_LIB_UI_SCENIC_FORWARD_DECLARATIONS_H_

#include <cstdint>

namespace scenic {

class Clock;
class CommandDispatcher;
class Scenic;
class Session;

using SessionId = uint64_t;

}  // namespace scenic

#endif  // GARNET_LIB_UI_SCENIC_FORWARD_DECLARATIONS_H_
