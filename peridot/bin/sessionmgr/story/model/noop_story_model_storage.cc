// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story/model/noop_story_model_storage.h"

#include <lib/fit/promise.h>

namespace modular {

NoopStoryModelStorage::NoopStoryModelStorage() = default;
NoopStoryModelStorage::~NoopStoryModelStorage() = default;

fit::promise<> NoopStoryModelStorage::Load() { return fit::make_ok_promise(); }

fit::promise<> NoopStoryModelStorage::Flush() { return fit::make_ok_promise(); }

fit::promise<> NoopStoryModelStorage::Execute(
    std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands) {
  return fit::make_promise([this, commands = std::move(commands)]() mutable {
           Observe(std::move(commands));
           return fit::ok();
         })
      .wrap_with(scope_);
}

}  // namespace modular
