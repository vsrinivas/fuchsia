// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job.h"

namespace zxdb {

Job::Job(Session* session) : ClientObject(session), weak_factory_(this) {}
Job::~Job() = default;

fxl::WeakPtr<Job> Job::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void Job::AddFilter(std::string filter) {
  // not implemented
}

JobFilter Job::RemoveFilter(uint32_t index) {
  FXL_DCHECK(filters_.size() > index);
  auto filter = filters_.at(index);
  filters_.erase(filters_.begin() + index);
  return filter;
}

}  // namespace zxdb
