// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scenic/tests/dummy_system.h"

namespace scenic_impl {
namespace test {

const char* DummySystem::kName = "DummySystem";

DummySystem::DummySystem(SystemContext context) : System(std::move(context)) {}

DummySystem::~DummySystem() = default;

CommandDispatcherUniquePtr DummySystem::CreateCommandDispatcher(
    scheduling::SessionId session_id, std::shared_ptr<EventReporter> event_reporter,
    std::shared_ptr<ErrorReporter> error_reporter) {
  ++num_dispatchers_;
  last_session_ = session_id;
  return CommandDispatcherUniquePtr(new DummyCommandDispatcher(),
                                    // Custom deleter.
                                    [](CommandDispatcher* cd) { delete cd; });
}

}  // namespace test
}  // namespace scenic_impl
