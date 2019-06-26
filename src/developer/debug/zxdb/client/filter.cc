// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/filter.h"

#include "src/developer/debug/zxdb/client/session.h"

namespace zxdb {

Filter::Filter(Session* session) : ClientObject(session) {}

void Filter::SetPattern(const std::string& pattern) {
  pattern_ = pattern;

  session()->system_impl().MarkFiltersDirty();
}

void Filter::SetJob(JobContext* job) {
  job_ = job ? std::optional(job->GetWeakPtr()) : std::nullopt;

  session()->system_impl().MarkFiltersDirty();
}

}  // namespace zxdb
