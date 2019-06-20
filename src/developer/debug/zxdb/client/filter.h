// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_

#include <optional>

#include "src/developer/debug/zxdb/client/job_context.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class Filter {
 public:
  void set_pattern(const std::string& pattern) { pattern_ = pattern; }
  void set_job(JobContext* job) {
    job_ = job ? std::optional(job->GetWeakPtr()) : std::nullopt;
  }
  const std::string& pattern() { return pattern_; }
  JobContext* job() { return job_ ? job_->get() : nullptr; }

 private:
  std::string pattern_;
  std::optional<fxl::WeakPtr<JobContext>> job_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_FILTER_H_
