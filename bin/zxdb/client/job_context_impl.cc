// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/job_context_impl.h"

#include <sstream>

#include "garnet/bin/zxdb/client/job_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
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
  if (GetJob())
    OnDetachReply(Err(), 0, nullptr);
}

JobContext::State JobContextImpl::GetState() const { return state_; }

Job* JobContextImpl::GetJob() const { return job_.get(); }

// static
void JobContextImpl::OnAttachReplyThunk(
    fxl::WeakPtr<JobContextImpl> job_context, Callback callback, const Err& err,
    uint64_t koid, uint32_t status, const std::string& job_name) {
  if (job_context) {
    job_context->OnAttachReply(std::move(callback), err, koid, status,
                               job_name);
  } else {
    // The reply that the job was launched came after the local
    // objects were destroyed.
    if (err.has_error()) {
      // Process not launched, forward the error.
      callback(job_context, err);
    } else {
      callback(job_context, Err("Warning: job attach race, extra job is "
                                "likely attached."));
    }
  }
}

void JobContextImpl::OnAttachReply(Callback callback, const Err& err,
                                   uint64_t koid, uint32_t status,
                                   const std::string& job_name) {
  FXL_DCHECK(state_ == State::kAttaching || state_ == State::kStarting);
  FXL_DCHECK(!job_.get());  // Shouldn't have a job.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status != 0) {
    // Error from launching.
    state_ = State::kNone;
    issue_err = Err(fxl::StringPrintf("Error attaching, status = %d.", status));
  } else {
    state_ = State::kRunning;
    job_ = std::make_unique<JobImpl>(this, koid, job_name);
  }

  if (callback)
    callback(GetWeakPtr(), issue_err);
}

void JobContextImpl::AttachInternal(debug_ipc::AttachRequest::Type type,
                                    uint64_t koid, Callback callback) {
  if (state_ != State::kNone) {
    // Avoid reentering caller to dispatch the error.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr),
                   Err("Can't attach, job is already running or starting."));
        });
    return;
  }

  state_ = State::kAttaching;

  debug_ipc::AttachRequest request;
  request.koid = koid;
  request.type = type;
  session()->remote_api()->Attach(
      request, [callback, weak_job_context = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AttachReply reply) {
        OnAttachReplyThunk(std::move(weak_job_context), std::move(callback),
                           err, reply.koid, reply.status, reply.name);
      });
}

void JobContextImpl::Attach(uint64_t koid, Callback callback) {
  AttachInternal(debug_ipc::AttachRequest::Type::kJob, koid, callback);
}

void JobContextImpl::AttachToComponentRoot(Callback callback) {
  AttachInternal(debug_ipc::AttachRequest::Type::kComponentRoot, 0, callback);
}

void JobContextImpl::Detach(Callback callback) {
  if (!job_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No job."));
        });
    return;
  }

  debug_ipc::DetachRequest request;
  request.koid = job_->GetKoid();
  request.type = debug_ipc::DetachRequest::Type::kJob;
  session()->remote_api()->Detach(
      request, [callback, weak_job_context = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) {
        if (weak_job_context) {
          weak_job_context->OnDetachReply(err, reply.status,
                                          std::move(callback));
        } else {
          // The reply that the process was launched came after the local
          // objects were destroyed. We're still OK to dispatch either way.
          callback(weak_job_context, err);
        }
      });
}

void JobContextImpl::OnDetachReply(const Err& err, uint32_t status,
                                   Callback callback) {
  FXL_DCHECK(job_.get());  // Should have a job.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status != 0) {
    // Error from detaching.
    // TODO(donosoc): Print error using ZxStatusToString
    issue_err = Err(fxl::StringPrintf("Error detaching, status = %d.", status));
  } else {
    // Successfully detached.
    state_ = State::kNone;
    job_.release();
  }

  if (callback)
    callback(GetWeakPtr(), issue_err);
}

}  // namespace zxdb
