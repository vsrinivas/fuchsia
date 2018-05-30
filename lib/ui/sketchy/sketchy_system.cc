// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/sketchy/sketchy_system.h"

namespace scenic {

SketchySystem::SketchySystem(SystemContext context, gfx::GfxSystem* gfx_system)
    : System(std::move(context)), gfx_system_(gfx_system) {}

SketchySystem::~SketchySystem() = default;

std::unique_ptr<CommandDispatcher> SketchySystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<SketchyCommandDispatcher>(std::move(context),
                                                    gfx_system_);
}

SketchyCommandDispatcher::SketchyCommandDispatcher(
    CommandDispatcherContext context, gfx::GfxSystem* gfx_system)
    : CommandDispatcher(std::move(context)), gfx_system_(gfx_system) {
  FXL_DCHECK(gfx_system_);
}

SketchyCommandDispatcher::~SketchyCommandDispatcher() = default;

void SketchyCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {}

}  // namespace scenic
