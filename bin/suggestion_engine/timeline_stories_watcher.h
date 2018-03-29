// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_WATCHER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_WATCHER_H_

#include <map>
#include <set>

#include <fuchsia/cpp/modular.h>
#include "lib/fidl/cpp/binding.h"

namespace modular {

// Watches a modular::StoryProvider for changes in the list of a user's
// Stories and makes the URLs of the Stories available to clients.
class TimelineStoriesWatcher : public modular::StoryProviderWatcher {
 public:
  TimelineStoriesWatcher(modular::StoryProviderPtr* story_provider);

  ~TimelineStoriesWatcher() override;

  const std::set<std::string>& StoryUrls() const { return story_urls_; }

  void SetWatcher(std::function<void()> watcher) { watcher_ = watcher; }

 private:
  void OnChange(modular::StoryInfo story_info,
                modular::StoryState state) override;
  void OnDelete(fidl::StringPtr story_id) override;

  fidl::Binding<StoryProviderWatcher> binding_;

  std::set<std::string> story_urls_;
  std::map<std::string, std::string> id_to_url_;

  std::function<void()> watcher_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_WATCHER_H_
