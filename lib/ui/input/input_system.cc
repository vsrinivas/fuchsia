// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/gfx_system.h"
#include "garnet/lib/ui/input/input_system.h"

#include "lib/fxl/logging.h"

namespace scenic {
namespace input {

InputSystem::InputSystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context)), gfx_system_(gfx_system) {
  FXL_CHECK(gfx_system_);
  FXL_LOG(INFO) << "Scenic input system started.";
}

InputSystem::~InputSystem() = default;

std::unique_ptr<CommandDispatcher> InputSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<InputCommandDispatcher>(std::move(context),
                                                  gfx_system_);
}

InputCommandDispatcher::InputCommandDispatcher(
    CommandDispatcherContext context, scenic::gfx::GfxSystem* gfx_system)
    : CommandDispatcher(std::move(context)), gfx_system_(gfx_system) {
  FXL_DCHECK(gfx_system_);
}

InputCommandDispatcher::~InputCommandDispatcher() = default;

void InputCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {
  FXL_DCHECK(command.Which() == fuchsia::ui::scenic::Command::Tag::kInput);
  FXL_VLOG(2) << "Scenic input - command received.";
}

}  // namespace input
}  // namespace scenic
