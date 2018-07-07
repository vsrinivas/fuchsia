// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <numeric>

#include <lib/async/default.h>

#include "garnet/bin/trace_manager/trace_session.h"
#include "lib/fidl/cpp/clone.h"
#include "lib/fxl/logging.h"

namespace tracing {

TraceSession::TraceSession(zx::socket destination,
                           fidl::VectorPtr<fidl::StringPtr> categories,
                           size_t trace_buffer_size,
                           fuchsia::tracelink::BufferingMode buffering_mode,
                           fit::closure abort_handler)
    : destination_(std::move(destination)),
      categories_(std::move(categories)),
      trace_buffer_size_(trace_buffer_size),
      buffering_mode_(buffering_mode),
      abort_handler_(std::move(abort_handler)),
      weak_ptr_factory_(this) {}

TraceSession::~TraceSession() {
  session_start_timeout_.Cancel();
  session_finalize_timeout_.Cancel();
  destination_.reset();
}

void TraceSession::WaitForProvidersToStart(fit::closure callback,
                                           zx::duration timeout) {
  start_callback_ = std::move(callback);
  session_start_timeout_.PostDelayed(async_get_default_dispatcher(), timeout);
}

void TraceSession::AddProvider(TraceProviderBundle* bundle) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  FXL_VLOG(1) << "Adding provider " << *bundle;

  tracees_.emplace_back(std::make_unique<Tracee>(bundle));
  fidl::VectorPtr<fidl::StringPtr> categories_clone;
  fidl::Clone(categories_, &categories_clone);
  if (!tracees_.back()->Start(
          std::move(categories_clone), trace_buffer_size_, buffering_mode_,
          [weak = weak_ptr_factory_.GetWeakPtr(), bundle]() {
            if (weak)
              weak->CheckAllProvidersStarted();
          },
          [weak = weak_ptr_factory_.GetWeakPtr(), bundle]() {
            if (weak)
              weak->FinishProvider(bundle);
          })) {
    tracees_.pop_back();
  } else {
    // We haven't fully started at this point, we still have to wait for
    // the provider to indicate it has started, but for our purposes we have
    // started "enough".
    TransitionToState(State::kStarted);
  }
}

void TraceSession::RemoveDeadProvider(TraceProviderBundle* bundle) {
  if (!(state_ == State::kStarted || state_ == State::kStopping))
    return;
  FinishProvider(bundle);
}

void TraceSession::Stop(fit::closure done_callback, zx::duration timeout) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  FXL_VLOG(1) << "Stopping trace";

  TransitionToState(State::kStopping);
  done_callback_ = std::move(done_callback);

  // Walk through all remaining tracees and send out their buffers.
  for (const auto& tracee : tracees_)
    tracee->Stop();

  session_finalize_timeout_.PostDelayed(async_get_default_dispatcher(), timeout);
  FinishSessionIfEmpty();
}

void TraceSession::NotifyStarted() {
  if (start_callback_) {
    FXL_VLOG(1) << "Marking session as having started";
    session_start_timeout_.Cancel();
    auto start_callback = std::move(start_callback_);
    start_callback();
  }
}

void TraceSession::Abort() {
  FXL_VLOG(1) << "Marking session as having aborted";
  TransitionToState(State::kStopped);
  tracees_.clear();
  abort_handler_();
}

// Called when a provider state change is detected.
// This includes "failed" as well as "started".

void TraceSession::CheckAllProvidersStarted() {
  bool all_started = std::accumulate(
      tracees_.begin(), tracees_.end(), true,
      [](bool value, const auto& tracee) {
        bool ready = (tracee->state() == Tracee::State::kStarted ||
                      // If a provider fails to start continue tracing.
                      // TODO(TO-530): We should still record what providers
                      // failed to start.
                      tracee->state() == Tracee::State::kStopped);
        FXL_VLOG(2) << "tracee " << *tracee->bundle() << (ready ? "" : " not")
                    << " ready";
        return value && ready;
      });

  if (all_started) {
    FXL_VLOG(2) << "All providers reporting started";
    NotifyStarted();
  }
}

void TraceSession::FinishProvider(TraceProviderBundle* bundle) {
  auto it =
      std::find_if(tracees_.begin(), tracees_.end(),
                   [bundle](const auto& tracee) { return *tracee == bundle; });

  if (it != tracees_.end()) {
    if (destination_) {
      switch ((*it)->TransferRecords(destination_)) {
        case Tracee::TransferStatus::kComplete:
          break;
        case Tracee::TransferStatus::kCorrupted:
          FXL_LOG(ERROR) << "Encountered unrecoverable error writing socket, "
                            "aborting trace";
          Abort();
          return;
        case Tracee::TransferStatus::kReceiverDead:
          FXL_LOG(ERROR) << "Peer is closed, aborting trace";
          Abort();
          return;
        default:
          break;
      }
    }
    tracees_.erase(it);
  }

  if (state_ != State::kStopping) {
    // A trace provider may have entered the finished state without having
    // first successfully started. Check whether all remaining providers have
    // now started.
    CheckAllProvidersStarted();
  }

  FinishSessionIfEmpty();
}

void TraceSession::FinishSessionIfEmpty() {
  if (state_ == State::kStopping && tracees_.empty()) {
    FXL_VLOG(1) << "Marking session as stopped, no more tracees";
    TransitionToState(State::kStopped);
    session_finalize_timeout_.Cancel();
    done_callback_();
  }
}

void TraceSession::FinishSessionDueToTimeout() {
  // We do not consider pending_start_tracees_ here as we only
  // stop them as a best effort.
  if (state_ == State::kStopping && !tracees_.empty()) {
    FXL_VLOG(1)
        << "Marking session as stopped, timed out waiting for tracee(s)";
    TransitionToState(State::kStopped);
    for (auto& tracee : tracees_) {
      if (tracee->state() != Tracee::State::kStopped)
        FXL_LOG(WARNING) << "Timed out waiting for trace provider "
                         << *tracee->bundle() << " to finish";
    }
    done_callback_();
  }
}

void TraceSession::TransitionToState(State new_state) {
  FXL_VLOG(2) << "Transitioning from " << state_ << " to " << new_state;
  state_ = new_state;
}

void TraceSession::SessionStartTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                       zx_status_t status) {
  FXL_LOG(WARNING) << "Waiting for start timed out.";
  NotifyStarted();
}

void TraceSession::SessionFinalizeTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                                          zx_status_t status) {
  FinishSessionDueToTimeout();
}

std::ostream& operator<<(std::ostream& out, TraceSession::State state) {
  switch (state) {
    case TraceSession::State::kReady:
      out << "ready";
      break;
    case TraceSession::State::kStarted:
      out << "started";
      break;
    case TraceSession::State::kStopping:
      out << "stopping";
      break;
    case TraceSession::State::kStopped:
      out << "stopped";
      break;
  }

  return out;
}

}  // namespace tracing
