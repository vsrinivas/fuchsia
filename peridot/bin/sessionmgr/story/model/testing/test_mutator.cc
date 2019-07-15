// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story/model/testing/test_mutator.h"

namespace modular {

std::unique_ptr<StoryMutator> TestMutator::Create(TestMutator** ptr) {
  auto mutator = std::make_unique<TestMutator>();
  *ptr = mutator.get();
  return mutator;
}

fit::consumer<> TestMutator::ExecuteInternal(
    std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands) {
  fit::bridge<> bridge;
  ExecuteCall call{.completer = std::move(bridge.completer), .commands = std::move(commands)};
  execute_calls.push_back(std::move(call));
  return std::move(bridge.consumer);
}

}  // namespace modular
