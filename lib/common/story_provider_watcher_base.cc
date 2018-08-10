// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "story_provider_watcher_base.h"

#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/string.h>

namespace modular {

StoryProviderWatcherBase::StoryProviderWatcherBase()
    : continue_([] {}), binding_(this) {}

StoryProviderWatcherBase::~StoryProviderWatcherBase() = default;

void StoryProviderWatcherBase::Continue(std::function<void()> at) {
  continue_ = std::move(at);
}

void StoryProviderWatcherBase::Watch(
    fuchsia::modular::StoryProviderPtr* const story_provider) {
  (*story_provider)->Watch(binding_.NewBinding());
}

void StoryProviderWatcherBase::Reset() { binding_.Unbind(); }

void StoryProviderWatcherBase::OnDelete(::fidl::StringPtr /*story_id*/) {}

void StoryProviderWatcherBase::OnChange(
    fuchsia::modular::StoryInfo /*story_info*/,
    fuchsia::modular::StoryState /*story_state*/,
    fuchsia::modular::StoryVisibilityState /*story_visibility_state*/) {
  continue_();
}

}  // namespace modular
