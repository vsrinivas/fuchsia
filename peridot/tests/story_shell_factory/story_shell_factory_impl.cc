// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/story_shell_factory/story_shell_factory_impl.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

namespace modular {
namespace testing {

StoryShellFactoryImpl::StoryShellFactoryImpl() = default;
StoryShellFactoryImpl::~StoryShellFactoryImpl() = default;

fidl::InterfaceRequestHandler<fuchsia::modular::StoryShellFactory>
StoryShellFactoryImpl::GetHandler() {
  return bindings_.GetHandler(this);
}

// |StoryShellFactory|
void StoryShellFactoryImpl::AttachStory(std::string story_id,
                                        StoryShellRequest request) {
  on_attach_story_(std::move(story_id), std::move(request));
}

// |StoryShellFactory|
void StoryShellFactoryImpl::DetachStory(std::string story_id,
                                        std::function<void()> done) {
  on_detach_story_();

  // Used to simulate a sluggish shell that hits the timeout.
  async::PostDelayedTask(async_get_default_dispatcher(), std::move(done),
                         detach_delay_);
}

}  // namespace testing
}  // namespace modular
