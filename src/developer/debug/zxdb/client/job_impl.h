// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_IMPL_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_IMPL_H_

#include "src/developer/debug/zxdb/client/job.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace zxdb {

class JobContextImpl;

// TODO(bug 43794) combine JobContext[Impl] and Job[Impl] objects.
class JobImpl : public Job {
 public:
  JobImpl(JobContextImpl* job_context, uint64_t koid, const std::string& name);
  ~JobImpl() override;

  JobContextImpl* job_context() const { return job_context_; }

  // Job implementation:
  JobContext* GetJobContext() const override;
  uint64_t GetKoid() const override;
  const std::string& GetName() const override;

 private:
  JobContextImpl* const job_context_;  // The job_context owns |this|.
  const uint64_t koid_;
  std::string name_;

  fxl::WeakPtrFactory<JobImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(JobImpl);
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_JOB_IMPL_H_
