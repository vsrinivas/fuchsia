// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_FILTER_H_
#define PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_FILTER_H_

#include <fuchsia/cpp/modular.h>

namespace modular {

class TimelineStoriesWatcher;

// Filters Proposals that are trying to launch a Story that already exists
// in the user's timeline.
class TimelineStoriesFilter {
 public:
  // Does not take ownership of 'timeline_stories_watcher'.
  explicit TimelineStoriesFilter(
      TimelineStoriesWatcher* timeline_stories_watcher);

  ~TimelineStoriesFilter();

  // "is a" ProposalFilter by virtue of this method. Returns false if
  // this Proposal should be excluded.
  bool operator()(const Proposal& proposal);

 private:
  TimelineStoriesWatcher* timeline_stories_watcher_;
};

}  // namespace modular

#endif  // PERIDOT_BIN_SUGGESTION_ENGINE_TIMELINE_STORIES_FILTER_H_
