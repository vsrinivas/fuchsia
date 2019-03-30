// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job_impl.h"

#include "src/developer/debug/zxdb/client/job_context_impl.h"

namespace zxdb {

JobImpl::JobImpl(JobContextImpl* job_context, uint64_t koid,
                 const std::string& name)
    : Job(job_context->session()),
      job_context_(job_context),
      koid_(koid),
      name_(name),
      weak_factory_(this) {}

JobImpl::~JobImpl() {}

JobContext* JobImpl::GetJobContext() const { return job_context_; }

uint64_t JobImpl::GetKoid() const { return koid_; }

const std::string& JobImpl::GetName() const { return name_; }

}  // namespace zxdb
