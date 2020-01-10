// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_OBSERVER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_OBSERVER_H_

#include <optional>

namespace zxdb {

class Filter;
class JobContext;

class FilterObserver {
 public:
  // Called when a filter is first created and activated.
  virtual void DidCreateFilter(Filter* filter) {}

  // Called when the job or pattern of a filter changes. The previous job is given to us as well,
  // unless it was invalid. It is an optional containing null if the filter used to match all jobs
  // and equal to the current job if the job hasn't changed. It is a std::nullopt if the filter used
  // to be invalid.
  virtual void DidChangeFilter(Filter* filter, std::optional<JobContext*> previous_job) {}

  // Called when a filter has been deactivated and is about to be destroyed.
  virtual void WillDestroyFilter(Filter* filter) {}

  // Called when a filter request comes back with a the list of processes currently running in the
  // agent that match the filter request.
  virtual void OnFilterMatches(JobContext* job, const std::vector<uint64_t>& matched_pids) {}
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_OBSERVER_H_
