// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/internal/fields.h"
#include "apps/tracing/src/trace_manager/trace_session.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace tracing {

TraceSession::TraceSession(mx::socket destination,
                           fidl::Array<fidl::String> categories,
                           size_t trace_buffer_size,
                           ftl::Closure abort_handler)
    : destination_(std::move(destination)),
      categories_(std::move(categories)),
      trace_buffer_size_(trace_buffer_size),
      buffer_(trace_buffer_size_),
      abort_handler_(std::move(abort_handler)),
      weak_ptr_factory_(this) {}

TraceSession::~TraceSession() {
  destination_.reset();
}

void TraceSession::AddProvider(TraceProviderBundle* bundle) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  tracees_.emplace_back(bundle);
  if (!tracees_.back().Start(
          trace_buffer_size_, categories_.Clone(),
          [ weak = weak_ptr_factory_.GetWeakPtr(), bundle ]() {
            if (weak) {
              weak->FinishProvider(bundle);
              weak->FinishSessionIfEmpty();
            }
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

void TraceSession::Stop(ftl::Closure done_callback,
                        const ftl::TimeDelta& timeout) {
  if (!(state_ == State::kReady || state_ == State::kStarted))
    return;

  TransitionToState(State::kStopping);
  done_callback_ = std::move(done_callback);

  // Walk through all remaining tracees and send out their buffers.
  for (auto it = tracees_.begin(); it != tracees_.end(); ++it) {
    it->Stop();
  }

  session_finalize_timeout_.Start(
      mtl::MessageLoop::GetCurrent()->task_runner().get(),
      [weak = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak)
          weak->done_callback_();
      },
      timeout);

  FinishSessionIfEmpty();
}

void TraceSession::Abort() {
  TransitionToState(State::kStopped);
  tracees_.clear();
  abort_handler_();
}

void TraceSession::FinishProvider(TraceProviderBundle* bundle) {
  auto it =
      std::find_if(tracees_.begin(), tracees_.end(),
                   [bundle](const auto& tracee) { return tracee == bundle; });

  if (it != tracees_.end()) {
    if (destination_) {
      switch (it->TransferRecords(destination_)) {
        case Tracee::TransferStatus::kComplete:
          break;
        case Tracee::TransferStatus::kCorrupted:
          FTL_LOG(ERROR) << "Encountered unrecoverable error writing socket, "
                            "aborting trace";
          Abort();
          return;
        case Tracee::TransferStatus::kReceiverDead:
          FTL_LOG(ERROR) << "Peer is closed, aborting trace";
          Abort();
          return;
        default:
          break;
      }
    }
    tracees_.erase(it);
  }

  FinishSessionIfEmpty();
}

void TraceSession::FinishSessionIfEmpty() {
  if (state_ == State::kStopping && tracees_.empty()) {
    TransitionToState(State::kStopped);
    session_finalize_timeout_.Stop();
    done_callback_();
  }
}

void TraceSession::TransitionToState(State new_state) {
  FTL_VLOG(2) << "Transitioning from " << static_cast<uint32_t>(state_)
              << " to " << static_cast<uint32_t>(new_state);
  state_ = new_state;
}

}  // namespace tracing
