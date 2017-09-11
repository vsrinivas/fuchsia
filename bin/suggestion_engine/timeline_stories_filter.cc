// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/maxwell/src/suggestion_engine/timeline_stories_filter.h"

#include "apps/maxwell/src/suggestion_engine/timeline_stories_watcher.h"

namespace maxwell {

TimelineStoriesFilter::TimelineStoriesFilter(
    TimelineStoriesWatcher* timeline_stories_watcher)
    : timeline_stories_watcher_(timeline_stories_watcher) {
  FXL_CHECK(timeline_stories_watcher_);
}

bool TimelineStoriesFilter::operator()(const Proposal& proposal) {
  // TODO(rosswang) Why can't we foreach a const fidl::Array???
  for (size_t i = 0; i < proposal.on_selected.size(); i++) {
    const auto& action = proposal.on_selected[i];
    if (action->is_create_story()) {
      const auto& create_story = action->get_create_story();
      const auto& module_url = create_story->module_id.get();
      if (timeline_stories_watcher_->StoryUrls().count(module_url) > 0) {
        return false;
      }
    }
  }

  return true;
}

}  // namespace maxwell
