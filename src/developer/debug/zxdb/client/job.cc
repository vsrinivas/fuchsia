// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/job.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/zx_status.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/remote_api.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/setting_schema_definition.h"
#include "src/developer/debug/zxdb/client/system.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Job::Job(Session* session, bool is_implicit_root)
    : ClientObject(session), is_implicit_root_(is_implicit_root), weak_factory_(this) {
  session->AddFilterObserver(this);
  RefreshFilters();
}

Job::~Job() {
  // If the job is still running, make sure we broadcast terminated notifications before deleting
  // everything.
  ImplicitlyDetach();
  session()->RemoveFilterObserver(this);
}

fxl::WeakPtr<Job> Job::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

void Job::ImplicitlyDetach() {
  if (state_ == State::kAttached)
    OnDetachReply(Err(), debug::Status(), [](fxl::WeakPtr<Job>, const Err&) {});
}

// static
void Job::OnAttachReplyThunk(fxl::WeakPtr<Job> job, Callback callback, const Err& err,
                             uint64_t koid, const debug::Status& status,
                             const std::string& job_name) {
  if (job) {
    job->OnAttachReply(std::move(callback), err, koid, status, job_name);
    if (!job->filters_.empty()) {
      job->SendAndUpdateFilters(job->filters_, true);
    }
  } else {
    // The reply that the job was launched came after the local objects were destroyed.
    if (err.has_error()) {
      // Process not launched, forward the error.
      callback(job, err);
    } else {
      callback(job, Err("Warning: job attach race, extra job is likely attached."));
    }
  }
}

void Job::OnAttachReply(Callback callback, const Err& err, uint64_t koid,
                        const debug::Status& status, const std::string& job_name) {
  FX_DCHECK(state_ == State::kAttaching);

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status.has_error()) {
    // Error from launching.
    state_ = State::kNone;
    issue_err = Err("Error attaching: " + status.message());
  } else {
    state_ = State::kAttached;
    koid_ = koid;
    name_ = job_name;
  }

  callback(GetWeakPtr(), issue_err);
}

void Job::AttachInternal(debug_ipc::TaskType type, uint64_t koid, Callback callback) {
  if (state_ != State::kNone) {
    // Avoid reentering caller to dispatch the error.
    debug::MessageLoop::Current()->PostTask(
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
      request, [callback = std::move(callback), weak_job = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::AttachReply reply) mutable {
        OnAttachReplyThunk(std::move(weak_job), std::move(callback), err, reply.koid, reply.status,
                           reply.name);
      });
}

void Job::Attach(uint64_t koid, Callback callback) {
  AttachInternal(debug_ipc::TaskType::kJob, koid, std::move(callback));
}

void Job::AttachToSystemRoot(Callback callback) {
  AttachInternal(debug_ipc::TaskType::kSystemRoot, 0, std::move(callback));
}

void Job::AttachToComponentRoot(Callback callback) {
  AttachInternal(debug_ipc::TaskType::kComponentRoot, 0, std::move(callback));
}

void Job::AttachForTesting(uint64_t koid, const std::string& name) {
  state_ = State::kAttached;
  koid_ = koid;
  name_ = name;
}

void Job::Detach(Callback callback) {
  if (state_ != State::kAttached) {
    debug::MessageLoop::Current()->PostTask(
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
  request.koid = koid_;
  request.type = debug_ipc::TaskType::kJob;
  session()->remote_api()->Detach(
      request, [callback = std::move(callback), weak_job = weak_factory_.GetWeakPtr()](
                   const Err& err, debug_ipc::DetachReply reply) mutable {
        if (weak_job) {
          weak_job->OnDetachReply(err, reply.status, std::move(callback));
        } else {
          // The reply that the process was launched came after the local objects were destroyed.
          // We're still OK to dispatch either way.
          callback(weak_job, err);
        }
      });
}

void Job::SendAndUpdateFilters(std::vector<std::string> filters) {
  SendAndUpdateFilters(filters, last_filter_set_failed_);
}

void Job::SendAndUpdateFilters(std::vector<std::string> filters, bool force_send) {
  last_filter_set_failed_ = false;

  if (state_ != State::kAttached) {
    filters_ = std::move(filters);
    return;
  }

  DEBUG_LOG(Job) << "Updating filters for job " << name_;
  if (!force_send && filters_ == filters) {
    return;
  }

  debug_ipc::JobFilterRequest request;
  request.job_koid = koid_;
  request.filters = filters;
  session()->remote_api()->JobFilter(request, [filters, weak_job = weak_factory_.GetWeakPtr()](
                                                  const Err& err, debug_ipc::JobFilterReply reply) {
    if (reply.status.has_error()) {
      FX_LOGS(ERROR) << "Error adding filter: " << reply.status.message();

      // Agent failed, mark that we had trouble setting filters and return.
      if (weak_job)
        weak_job->last_filter_set_failed_ = true;
      return;
    }

    if (weak_job)
      weak_job->filters_ = std::move(filters);

    // Let the observers know that we had some matches.
    if (!reply.matched_processes.empty()) {
      for (FilterObserver& filter_observer : weak_job->session()->filter_observers()) {
        filter_observer.OnFilterMatches(weak_job.get(), reply.matched_processes);
      }
    }
  });
}

void Job::OnDetachReply(const Err& err, const debug::Status& status, Callback callback) {
  FX_DCHECK(state_ == State::kAttached);  // Should have a job.

  Err issue_err;  // Error to send in callback.
  if (err.has_error()) {
    // Error from transport.
    state_ = State::kNone;
    issue_err = err;
  } else if (status.has_error()) {
    // Error from detaching.
    issue_err = Err("Error detaching: " + status.message());
  } else {
    // Successfully detached.
    state_ = State::kNone;
  }

  if (state_ == State::kNone) {
    koid_ = 0;
    name_.clear();
  }

  callback(GetWeakPtr(), issue_err);
}

void Job::DidCreateFilter(Filter* filter) {
  if (!filter->is_valid()) {
    return;
  }

  if (filter->job() && filter->job() != this) {
    return;
  }

  RefreshFilters();
}

void Job::DidChangeFilter(Filter* filter, std::optional<Job*> previous_job) {
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

void Job::WillDestroyFilter(Filter* filter) {
  // Same process.
  DidCreateFilter(filter);
}

void Job::RefreshFilters() {
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
