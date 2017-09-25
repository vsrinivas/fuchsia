// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "story_provider_watcher_base.h"

#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/string.h"

namespace modular {

StoryProviderWatcherBase::StoryProviderWatcherBase()
    : continue_([] {}), binding_(this) {}

StoryProviderWatcherBase::~StoryProviderWatcherBase() = default;

void StoryProviderWatcherBase::Continue(std::function<void()> at) {
  continue_ = std::move(at);
}

void StoryProviderWatcherBase::Watch(
    modular::StoryProviderPtr* const story_provider) {
  (*story_provider)->Watch(binding_.NewBinding());
}

void StoryProviderWatcherBase::Reset() {
  binding_.Close();
}

void StoryProviderWatcherBase::OnDelete(const ::fidl::String& /*story_id*/) {}

void StoryProviderWatcherBase::OnChange(modular::StoryInfoPtr /*story_info*/,
                                        modular::StoryState /*story_state*/) {
  continue_();
}

}  // namespace modular
