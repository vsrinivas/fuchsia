// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/job_context_impl.h"

#include <sstream>

#include "garnet/bin/zxdb/client/job_impl.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "garnet/lib/debug_ipc/helper/message_loop.h"
#include "garnet/public/lib/fxl/logging.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

JobContextImpl::JobContextImpl(SystemImpl* system)
    : JobContext(system->session()),
      system_(system),
      impl_weak_factory_(this) {}

JobContextImpl::~JobContextImpl() {
  // If the job is still running, make sure we broadcast terminated
  // notifications before deleting everything.
  ImplicitlyDetach();
}

std::unique_ptr<JobContextImpl> JobContextImpl::Clone(SystemImpl* system) {
  auto result = std::make_unique<JobContextImpl>(system);
  return result;
}

void JobContextImpl::ImplicitlyDetach() {
  // TODO(DX-322): detach
}

JobContext::State JobContextImpl::GetState() const { return state_; }

Job* JobContextImpl::GetJob() const { return job_.get(); }

void JobContextImpl::Attach(uint64_t koid, Callback callback) {
  callback(GetWeakPtr(), Err("Can't attach, not implemented."));
  return;

  // TODO(DX-322): attach
}

void JobContextImpl::Detach(Callback callback) {
  if (!job_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No job."));
        });
    return;
  }

  // TODO(DX-322): detach
}

}  // namespace zxdb
