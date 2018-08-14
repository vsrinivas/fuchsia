// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/input_system.h"

#include "lib/fxl/logging.h"

namespace scenic {
namespace input {

InputSystem::InputSystem(SystemContext context,
                         bool initialized_after_construction)
    : System(std::move(context), initialized_after_construction) {
      FXL_LOG(INFO) << "Scenic input system started.";
}

InputSystem::~InputSystem() {
}

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return nullptr;
}

}  // namespace input
}  // namespace scenic
