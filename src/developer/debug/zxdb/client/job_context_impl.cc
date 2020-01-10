// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job_context_impl.h"

#include <sstream>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/job_impl.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system_impl.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

JobContextImpl::JobContextImpl(SystemImpl* system, bool is_implicit_root)
    : JobContext(system->session()),
      system_(system),
      is_implicit_root_(is_implicit_root),
      impl_weak_factory_(this) {
  session()->AddFilterObserver(this);
  RefreshFilters();
}

JobContextImpl::~JobContextImpl() {
  // If the job is still running, make sure we broadcast terminated notifications before deleting
  // everything.
  ImplicitlyDetach();
  session()->RemoveFilterObserver(this);
}

void JobContextImpl::ImplicitlyDetach() {
  if (GetJob())
    OnDetachReply(Err(), 0, [](fxl::WeakPtr<JobContext>, const Err&) {});
}

JobContext::State JobContextImpl::GetState() const { return state_; }

Job* JobContextImpl::GetJob() const { return job_.get(); }

// static
void JobContextImpl::OnAttachReplyThunk(fxl::WeakPtr<JobContextImpl> job_context, Callback callback,
                                        const Err& err, uint64_t koid, uint32_t status,
                                        const std::string& job_name) {
  if (job_context) {
    job_context->OnAttachReply(std::move(callback), err, koid, status, job_name);
    if (!job_context->filters_.empty()) {
      job_context->SendAndUpdateFilters(job_context->filters_, true);
    }
  } else {
    // The reply that the job was launched came after the local objects were destroyed.
    if (err.has_error()) {
      // Process not launched, forward the error.
      callback(job_context, err);
    } else {
      callback(job_context, Err("Warning: job attach race, extra job is "
                                "likely attached."));
    }
  }
}

void JobContextImpl::OnAttachReply(Callback callback, const Err& err, uint64_t koid,
                                   uint32_t status, const std::string& job_name) {
  FXL_DCHECK(state_ == State::kAttaching);
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
    state_ = State::kAttached;
    job_ = std::make_unique<JobImpl>(this, koid, job_name);
  }

  callback(GetWeakPtr(), issue_err);
}

void JobContextImpl::AttachInternal(debug_ipc::TaskType type, uint64_t koid, Callback callback) {
  if (state_ != State::kNone) {
    // Avoid reentering caller to dispatch the error.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr), Err("Can't attach, job is already running or starting."));
        });
    return;
  }

  state_ = State::kAttaching;

  debug_ipc::AttachRequest request;
  request.koid = koid;
  request.type = type;
  session()->remote_api()->Attach(
      request, [callback = std::move(callback), weak_job_context = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AttachReply reply) mutable {
        OnAttachReplyThunk(std::move(weak_job_context), std::move(callback), err, reply.koid,
                           reply.status, reply.name);
      });
}

void JobContextImpl::Attach(uint64_t koid, Callback callback) {
  AttachInternal(debug_ipc::TaskType::kJob, koid, std::move(callback));
}

void JobContextImpl::AttachToSystemRoot(Callback callback) {
  AttachInternal(debug_ipc::TaskType::kSystemRoot, 0, std::move(callback));
}

void JobContextImpl::AttachToComponentRoot(Callback callback) {
  AttachInternal(debug_ipc::TaskType::kComponentRoot, 0, std::move(callback));
}

void JobContextImpl::AddJobImplForTesting(uint64_t koid, const std::string& name) {
  job_ = std::make_unique<JobImpl>(this, koid, name);
}

void JobContextImpl::Detach(Callback callback) {
  if (!job_.get()) {
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE, [callback = std::move(callback), weak_ptr = GetWeakPtr()]() mutable {
          callback(std::move(weak_ptr), Err("Error detaching: No job."));
        });
    return;
  }

  // This job could have been the one automatically created. If the user explicitly detaches it, the
  // user is taking control over what job it's attached to so we don't want to track it implicitly
  // any more.
  is_implicit_root_ = false;

  debug_ipc::DetachRequest request;
  request.koid = job_->GetKoid();
  request.type = debug_ipc::TaskType::kJob;
  session()->remote_api()->Detach(
      request, [callback = std::move(callback), weak_job_context = impl_weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) mutable {
        if (weak_job_context) {
          weak_job_context->OnDetachReply(err, reply.status, std::move(callback));
        } else {
          // The reply that the process was launched came after the local objects were destroyed.
          // We're still OK to dispatch either way.
          callback(weak_job_context, err);
        }
      });
}

void JobContextImpl::SendAndUpdateFilters(std::vector<std::string> filters) {
  SendAndUpdateFilters(filters, last_filter_set_failed_);
}

void JobContextImpl::SendAndUpdateFilters(std::vector<std::string> filters, bool force_send) {
  last_filter_set_failed_ = false;

  if (!job_.get()) {
    filters_ = std::move(filters);
    return;
  }

  DEBUG_LOG(Job) << "Updating filters for job " << job_->GetName();
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
          FXL_LOG(ERROR) << "Error adding filter: " << debug_ipc::ZxStatusToString(reply.status);

          // Agent failed, mark that we had trouble setting filters and return.
          if (weak_job_context)
            weak_job_context->last_filter_set_failed_ = true;
          return;
        }

        if (weak_job_context)
          weak_job_context->filters_ = std::move(filters);

        // Let the observers know that we had some matches.
        if (!reply.matched_processes.empty()) {
          for (FilterObserver& filter_observer : weak_job_context->session()->filter_observers()) {
            filter_observer.OnFilterMatches(weak_job_context.get(), reply.matched_processes);
          }
        }
      });
}

void JobContextImpl::OnDetachReply(const Err& err, uint32_t status, Callback callback) {
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
    job_.reset();
  }

  callback(GetWeakPtr(), issue_err);
}

void JobContextImpl::DidCreateFilter(Filter* filter) {
  if (!filter->is_valid()) {
    return;
  }

  if (filter->job() && filter->job() != this) {
    return;
  }

  RefreshFilters();
}

void JobContextImpl::DidChangeFilter(Filter* filter, std::optional<JobContext*> previous_job) {
  if (!filter->is_valid()) {
    // The filter only becomes invalid if the job it applies to dies. We're not dead, so this filter
    // never applied to us.
    return;
  }

  if ((previous_job && (*previous_job == this || !*previous_job)) || filter->job() == this ||
      !filter->job()) {
    RefreshFilters();
  }
}

void JobContextImpl::WillDestroyFilter(Filter* filter) {
  // Same process.
  DidCreateFilter(filter);
}

void JobContextImpl::RefreshFilters() {
  std::vector<std::string> items;

  for (const auto& filter : session()->system().GetFilters()) {
    if (!filter->is_valid()) {
      continue;
    }

    if (filter->job() == this || !filter->job()) {
      // The IPC protocol uses the empty string to mean "all processes".
      if (filter->pattern() == Filter::kAllProcessesPattern)
        items.emplace_back();
      else
        items.push_back(filter->pattern());
    }
  }

  SendAndUpdateFilters(items);
}

}  // namespace zxdb
