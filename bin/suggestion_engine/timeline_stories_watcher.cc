// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/suggestion_engine/timeline_stories_watcher.h"

#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fxl/logging.h"

namespace maxwell {

TimelineStoriesWatcher::TimelineStoriesWatcher(
    modular::StoryProviderPtr* story_provider)
    : binding_(this) {
  // Add ourselves as a watcher to the StoryProvider.
  fidl::InterfaceHandle<modular::StoryProviderWatcher> handle;
  binding_.Bind(&handle);
  (*story_provider)->Watch(std::move(handle));
}

void TimelineStoriesWatcher::OnChange(modular::StoryInfoPtr story_info,
                                      modular::StoryState state) {
  id_to_url_.emplace(story_info->id.get(), story_info->url.get());

  if (story_urls_.insert(story_info->url.get()).second) {
    if (watcher_)
      watcher_();
  }
}

void TimelineStoriesWatcher::OnDelete(const fidl::String& story_id) {
  auto it = id_to_url_.find(story_id.get());
  if (it != id_to_url_.end()) {
    // TODO(rosswang): use a multiset for story_urls (for stories with duplicate
    // URLs)
    story_urls_.erase(it->second);
    id_to_url_.erase(it);

    if (watcher_)
      watcher_();
  }
}

}  // namespace maxwell
