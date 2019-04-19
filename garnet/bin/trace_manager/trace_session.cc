// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/trace_manager/trace_session.h"

#include <lib/async/default.h>

#include <numeric>

#include "garnet/bin/trace_manager/util.h"
#include "lib/fidl/cpp/clone.h"
#include "src/lib/fxl/logging.h"
#include "trace-engine/fields.h"

namespace tracing {

TraceSession::TraceSession(zx::socket destination,
                           std::vector<std::string> categories,
                           size_t trace_buffer_size_megabytes,
                           fuchsia::tracelink::BufferingMode buffering_mode,
                           TraceProviderSpecMap&& provider_specs,
                           fit::closure abort_handler)
    : destination_(std::move(destination)),
      categories_(std::move(categories)),
      trace_buffer_size_megabytes_(trace_buffer_size_megabytes),
      buffering_mode_(buffering_mode),
      provider_specs_(std::move(provider_specs)),
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

  uint32_t buffer_size_megabytes = trace_buffer_size_megabytes_;
  auto spec_iter = provider_specs_.find(bundle->name);
  if (spec_iter != provider_specs_.end()) {
    const TraceProviderSpec* spec = &spec_iter->second;
    buffer_size_megabytes = spec->buffer_size_megabytes;
  }
  uint64_t buffer_size = buffer_size_megabytes * 1024 * 1024;

  FXL_VLOG(1) << "Adding provider " << *bundle << ", buffer size "
              << buffer_size_megabytes << "MB";

  tracees_.emplace_back(std::make_unique<Tracee>(this, bundle));
  fidl::VectorPtr<std::string> categories_clone;
  fidl::Clone(categories_, &categories_clone);
  if (!tracees_.back()->Start(
          std::move(categories_clone), buffer_size, buffering_mode_,
          [weak = weak_ptr_factory_.GetWeakPtr(), bundle]() {
            if (weak) {
              weak->OnProviderStarted(bundle);
            }
          },
          [weak = weak_ptr_factory_.GetWeakPtr(), bundle]() {
            if (weak) {
              weak->FinishProvider(bundle);
            }
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

  session_finalize_timeout_.PostDelayed(async_get_default_dispatcher(),
                                        timeout);
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

// Called when a provider reports that it has started.

void TraceSession::OnProviderStarted(TraceProviderBundle* bundle) {
  if (state_ == State::kReady || state_ == State::kStarted) {
    CheckAllProvidersStarted();
  } else {
    // Tracing stopped in the interim.
    auto it = std::find_if(
        tracees_.begin(), tracees_.end(),
        [bundle](const auto& tracee) { return *tracee == bundle; });

    if (it != tracees_.end()) {
      (*it)->Stop();
    }
  }
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
        case TransferStatus::kComplete:
          break;
        case TransferStatus::kProviderError:
          FXL_LOG(ERROR) << "Problem reading provider output, skipping";
          break;
        case TransferStatus::kWriteError:
          FXL_LOG(ERROR) << "Encountered unrecoverable error writing socket, "
                            "aborting trace";
          Abort();
          return;
        case TransferStatus::kReceiverDead:
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

void TraceSession::QueueTraceInfo() {
  async::PostTask(async_get_default_dispatcher(), [this]() {
    auto status = WriteMagicNumberRecord();
    if (status != TransferStatus::kComplete) {
      FXL_LOG(ERROR) << "Failed to write magic number record";
    }
  });
}

TransferStatus TraceSession::WriteMagicNumberRecord() {
  size_t num_words = 1u;
  std::vector<uint64_t> record(num_words);
  record[0] = trace::MagicNumberRecordFields::Type::Make(
                  trace::ToUnderlyingType(trace::RecordType::kMetadata)) |
              trace::MagicNumberRecordFields::RecordSize::Make(num_words) |
              trace::MagicNumberRecordFields::MetadataType::Make(
                  trace::ToUnderlyingType(trace::MetadataType::kTraceInfo)) |
              trace::MagicNumberRecordFields::TraceInfoType::Make(
                  trace::ToUnderlyingType(trace::TraceInfoType::kMagicNumber)) |
              trace::MagicNumberRecordFields::Magic::Make(trace::kMagicValue);
  return WriteBufferToSocket(destination_,
                             reinterpret_cast<uint8_t*>(record.data()),
                             trace::WordsToBytes(num_words));
}

void TraceSession::TransitionToState(State new_state) {
  FXL_VLOG(2) << "Transitioning from " << state_ << " to " << new_state;
  state_ = new_state;
}

void TraceSession::SessionStartTimeout(async_dispatcher_t* dispatcher,
                                       async::TaskBase* task,
                                       zx_status_t status) {
  FXL_LOG(WARNING) << "Waiting for start timed out.";
  NotifyStarted();
}

void TraceSession::SessionFinalizeTimeout(async_dispatcher_t* dispatcher,
                                          async::TaskBase* task,
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
