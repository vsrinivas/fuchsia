// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <numeric>

#include "garnet/bin/trace_manager/trace_session.h"
#include "lib/fxl/logging.h"
#include "lib/fsl/tasks/message_loop.h"

namespace tracing {

TraceSession::TraceSession(zx::socket destination,
                           fidl::Array<fidl::String> categories,
                           size_t trace_buffer_size,
                           fxl::Closure abort_handler)
    : destination_(std::move(destination)),
      categories_(std::move(categories)),
      trace_buffer_size_(trace_buffer_size),
      buffer_(trace_buffer_size_),
      abort_handler_(std::move(abort_handler)),
      weak_ptr_factory_(this) {}

TraceSession::~TraceSession() {
  destination_.reset();
}

void TraceSession::WaitForProvidersToStart(fxl::Closure callback,
                                           fxl::TimeDelta timeout) {
  start_callback_ = std::move(callback);
  session_start_timeout_.Start(
      fsl::MessageLoop::GetCurrent()->task_runner().get(),
      [weak = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak) {
          FXL_LOG(WARNING) << "Waiting for start timed out.";
          weak->NotifyStarted();
        }
      },
      std::move(timeout));
}

void TraceSession::AddProvider(TraceProviderBundle* bundle) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  FXL_VLOG(1) << "Adding provider " << *bundle;

  tracees_.emplace_back(std::make_unique<Tracee>(bundle));
  if (!tracees_.back()->Start(
          trace_buffer_size_, categories_.Clone(),
          [ weak = weak_ptr_factory_.GetWeakPtr(), bundle ](bool success) {
            if (weak)
              weak->CheckAllProvidersStarted();
          },
          [ weak = weak_ptr_factory_.GetWeakPtr(), bundle ]() {
            if (weak)
              weak->FinishProvider(bundle);
          })) {
    tracees_.pop_back();
  } else {
    TransitionToState(State::kStarted);
  }
}

void TraceSession::RemoveDeadProvider(TraceProviderBundle* bundle) {
  if (!(state_ == State::kStarted || state_ == State::kStopping))
    return;
  FinishProvider(bundle);
}

void TraceSession::Stop(fxl::Closure done_callback,
                        const fxl::TimeDelta& timeout) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  FXL_VLOG(1) << "Stopping trace";

  TransitionToState(State::kStopping);
  done_callback_ = std::move(done_callback);

  // Walk through all remaining tracees and send out their buffers.
  for (const auto& tracee : tracees_)
    tracee->Stop();

  session_finalize_timeout_.Start(
      fsl::MessageLoop::GetCurrent()->task_runner().get(),
      [weak = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak)
          weak->FinishSessionDueToTimeout();
      },
      timeout);

  FinishSessionIfEmpty();
}

void TraceSession::NotifyStarted() {
  if (start_callback_) {
    FXL_VLOG(1) << "Marking session as having started";
    session_start_timeout_.Stop();
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

void TraceSession::CheckAllProvidersStarted() {
  bool all_started = std::accumulate(
      tracees_.begin(), tracees_.end(), true,
      [](bool value, const auto& tracee) {
        return value && (tracee->state() == Tracee::State::kStarted ||
                         tracee->state() == Tracee::State::kStartAcknowledged);

      });

  if (all_started)
    NotifyStarted();
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

  CheckAllProvidersStarted();  // may have removed the last straggler
  FinishSessionIfEmpty();
}

void TraceSession::FinishSessionIfEmpty() {
  if (state_ == State::kStopping && tracees_.empty()) {
    FXL_VLOG(1) << "Marking session as stopped, no more tracees";
    TransitionToState(State::kStopped);
    session_finalize_timeout_.Stop();
    done_callback_();
  }
}

void TraceSession::FinishSessionDueToTimeout() {
  // We do not consider pending_start_tracees_ here as we only
  // stop them as a best effort.
  if (state_ == State::kStopping && !tracees_.empty()) {
    FXL_VLOG(1) << "Marking session as stopped, timed out waiting for tracee(s)";
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
  FXL_VLOG(2) << "Transitioning from " << state_
              << " to " << new_state;
  state_ = new_state;
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
