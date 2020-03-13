// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story/model/story_model_storage.h"

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

StoryModelStorage::StoryModelStorage() = default;
StoryModelStorage::~StoryModelStorage() = default;

void StoryModelStorage::SetObserveCallback(
    fit::function<void(std::vector<fuchsia::modular::storymodel::StoryModelMutation>)> callback) {
  observe_callback_ = std::move(callback);
}

void StoryModelStorage::Observe(
    std::vector<fuchsia::modular::storymodel::StoryModelMutation> commands) {
  FX_DCHECK(observe_callback_);
  observe_callback_(std::move(commands));
}

}  // namespace modular
