// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

Filter::Filter(Session* session) : ClientObject(session) {
  for (auto& observer : this->session()->filter_observers()) {
    observer.DidCreateFilter(this);
  }
}

Filter::~Filter() {
  for (auto& observer : session()->filter_observers()) {
    observer.WillDestroyFilter(this);
  }
}

void Filter::SetPattern(const std::string& pattern) {
  pattern_ = pattern;

  if (valid()) {
    for (auto& observer : session()->filter_observers()) {
      observer.OnChangedFilter(this, job());
    }
  }
}

void Filter::SetJob(JobContext* job) {
  std::optional<JobContext*> previous(this->job());

  if (!valid()) {
    previous = std::nullopt;
  }

  job_ = job ? std::optional(job->GetWeakPtr()) : std::nullopt;
  for (auto& observer : session()->filter_observers()) {
    observer.OnChangedFilter(this, previous);
  }
}

}  // namespace zxdb
