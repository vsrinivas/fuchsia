// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/scenic/tests/dummy_system.h"

namespace scenic_impl {
namespace test {

const char* DummySystem::kName = "DummySystem";

DummySystem::DummySystem(SystemContext context) : System(std::move(context)) {}

DummySystem::~DummySystem() = default;

CommandDispatcherUniquePtr DummySystem::CreateCommandDispatcher(CommandDispatcherContext context) {
  ++num_dispatchers_;
  last_session_ = context.session();
  return CommandDispatcherUniquePtr(new DummyCommandDispatcher(std::move(context)),
                                    // Custom deleter.
                                    [](CommandDispatcher* cd) { delete cd; });
}

DummyCommandDispatcher::DummyCommandDispatcher(CommandDispatcherContext context)
    : CommandDispatcher(std::move(context)) {}
DummyCommandDispatcher::~DummyCommandDispatcher() = default;

void DummyCommandDispatcher::DispatchCommand(fuchsia::ui::scenic::Command command) {}

}  // namespace test
}  // namespace scenic_impl
