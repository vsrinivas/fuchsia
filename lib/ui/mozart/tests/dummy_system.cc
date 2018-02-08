// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/mozart/tests/dummy_system.h"

namespace mz {
namespace test {

DummySystem::DummySystem(SystemContext context) : System(std::move(context)) {}

DummySystem::~DummySystem() = default;

std::unique_ptr<CommandDispatcher> DummySystem::CreateCommandDispatcher(
    CommandDispatcherContext context) {
  return std::make_unique<DummyCommandDispatcher>(std::move(context));
}

DummyCommandDispatcher::DummyCommandDispatcher(CommandDispatcherContext context)
    : CommandDispatcher(std::move(context)) {}
DummyCommandDispatcher::~DummyCommandDispatcher() = default;

bool DummyCommandDispatcher::ApplyCommand(
    const ui_mozart::CommandPtr& command) {
  return command->which() == ui_mozart::Command::Tag::DUMMY;
}

}  // namespace test
}  // namespace mz
