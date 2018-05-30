// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/dummy_system.h"

namespace scenic {
namespace test {

DummySystem::DummySystem(SystemContext context,
                         bool initialized_after_construction)
    : System(std::move(context), initialized_after_construction) {}

DummySystem::~DummySystem() = default;

std::unique_ptr<CommandDispatcher> DummySystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<DummyCommandDispatcher>(std::move(context));
}

DummyCommandDispatcher::DummyCommandDispatcher(CommandDispatcherContext context)
    : CommandDispatcher(std::move(context)) {}
DummyCommandDispatcher::~DummyCommandDispatcher() = default;

void DummyCommandDispatcher::DispatchCommand(
    fuchsia::ui::scenic::Command command) {}

}  // namespace test
}  // namespace scenic
