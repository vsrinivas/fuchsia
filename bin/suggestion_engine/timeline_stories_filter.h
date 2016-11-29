// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/maxwell/src/suggestion_engine/proposal_record.h"

namespace maxwell {

class TimelineStoriesWatcher;

namespace suggestion {

// Filters Proposals that are trying to launch a Story that already exists
// in the user's timeline.
class TimelineStoriesFilter {
 public:
  // Does not take ownership of 'timeline_stories_watcher'.
  explicit TimelineStoriesFilter(
      TimelineStoriesWatcher* timeline_stories_watcher);

  // "is a" ProposalFilter by virtue of this method. Returns false if
  // this Proposal should be excluded.
  bool operator()(const Proposal& proposal);

 private:
  TimelineStoriesWatcher* timeline_stories_watcher_;
};

}  // namespace suggestion
}  // namespace maxwell
