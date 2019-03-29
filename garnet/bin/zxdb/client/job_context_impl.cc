// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/job_context_impl.h"

#include <sstream>

#include "garnet/bin/zxdb/client/job_impl.h"
#include "garnet/bin/zxdb/client/remote_api.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/client/setting_schema_definition.h"
#include "garnet/bin/zxdb/client/system_impl.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "src/developer/debug/ipc/debug/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"

namespace zxdb {

JobContextImpl::JobContextImpl(SystemImpl* system,
                               bool is_implicit_component_root)
    : JobContext(system->session()),
      system_(system),
      is_implicit_component_root_(is_implicit_component_root),
      impl_weak_factory_(this) {
  settings_.AddObserver(ClientSettings::Job::kFilters, this);
}

JobContextImpl::~JobContextImpl() {
  // If the job is still running, make sure we broadcast terminated
  // notifications before deleting everything.
  ImplicitlyDetach();
}

std::unique_ptr<JobContextImpl> JobContextImpl::Clone(SystemImpl* system) {
  return std::make_unique<JobContextImpl>(system, false);
}

void JobContextImpl::ImplicitlyDetach() {
  if (GetJob())
    OnDetachReply(Err(), 0, [](fxl::WeakPtr<JobContext>, const Err&) {});
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
    if (!job_context->filters_.empty()) {
      job_context->SendAndUpdateFilters(job_context->filters_, true);
    }
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

  callback(GetWeakPtr(), issue_err);
}

void JobContextImpl::AttachInternal(debug_ipc::TaskType type, uint64_t koid,
                                    Callback callback) {
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
  AttachInternal(debug_ipc::TaskType::kJob, koid, callback);
}

void JobContextImpl::AttachToComponentRoot(Callback callback) {
  AttachInternal(debug_ipc::TaskType::kComponentRoot, 0, callback);
}

void JobContextImpl::Detach(Callback callback) {
  if (!job_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback, weak_ptr = GetWeakPtr()]() {
          callback(std::move(weak_ptr), Err("Error detaching: No job."));
        });
    return;
  }

  // This job could have been the one automatically created to watch the
  // component root. If the user explicitly detaches it, the user is taking
  // control over what job it's attached to so we don't want to track it
  // implicitly any more.
  is_implicit_component_root_ = false;

  debug_ipc::DetachRequest request;
  request.koid = job_->GetKoid();
  request.type = debug_ipc::TaskType::kJob;
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

void JobContextImpl::SendAndUpdateFilters(std::vector<std::string> filters,
                                          bool force_send) {
  if (!job_.get()) {
    filters_ = std::move(filters);
    return;
  }

  DEBUG_LOG() << "Updating filters for job " << job_->GetName();
  if (!force_send && filters_ == filters) {
    return;
  }

  debug_ipc::JobFilterRequest request;
  request.job_koid = job_->GetKoid();
  request.filters = filters;
  session()->remote_api()->JobFilter(
      request, [filters, weak_job_context = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::JobFilterReply reply) {
        if (reply.status != 0) {
          FXL_LOG(ERROR) << "Error adding filter: "
                         << debug_ipc::ZxStatusToString(reply.status);
          if (weak_job_context) {
            // Agent failed, reset filters in settings.
            // This will also trigger another callback but would be a no-op
            // because |force_send| would be false.
            weak_job_context->settings_.SetList(ClientSettings::Job::kFilters,
                                                weak_job_context->filters_);
          }
          return;
        }
        if (weak_job_context) {
          weak_job_context->filters_ = std::move(filters);
        }
      });
}

void JobContextImpl::OnSettingChanged(const SettingStore&,
                                      const std::string& setting_name) {
  FXL_CHECK(setting_name == ClientSettings::Job::kFilters);
  SendAndUpdateFilters(settings_.GetList(setting_name));
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

  callback(GetWeakPtr(), issue_err);
}

}  // namespace zxdb
