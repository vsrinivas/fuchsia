// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/views/view_system.h"

namespace scenic {

ViewSystem::ViewSystem(SystemContext context,
                       scenic::gfx::GfxSystem* scenic_system)
    : System(std::move(context)), scenic_system_(scenic_system) {}

ViewSystem::~ViewSystem() = default;

std::unique_ptr<CommandDispatcher> ViewSystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<ViewCommandDispatcher>(std::move(context),
                                                 scenic_system_);
}

ViewCommandDispatcher::ViewCommandDispatcher(
    CommandDispatcherContext context, scenic::gfx::GfxSystem* scenic_system)
    : CommandDispatcher(std::move(context)), scenic_system_(scenic_system) {
  FXL_DCHECK(scenic_system_);
}

ViewCommandDispatcher::~ViewCommandDispatcher() = default;

void ViewCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {}

}  // namespace scenic
