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
                           size_t trace_buffer_size)
    : destination_(std::move(destination)),
      categories_(std::move(categories)),
      trace_buffer_size_(trace_buffer_size),
      buffer_(trace_buffer_size_),
      weak_ptr_factory_(this) {}

TraceSession::~TraceSession() {
  destination_.reset();
}

void TraceSession::AddProvider(TraceProviderBundle* bundle) {
  if (done_callback_) {
    FTL_LOG(WARNING) << "Stop has already been requested";
    return;
  }

  tracees_.emplace_back(bundle);
  if (!tracees_.back().Start(trace_buffer_size_, categories_.Clone()))
    tracees_.pop_back();
}

void TraceSession::RemoveDeadProvider(TraceProviderBundle* bundle) {
  FinishProvider(bundle);
}

void TraceSession::Stop(ftl::Closure done_callback,
                        const ftl::TimeDelta& timeout) {
  done_callback_ = std::move(done_callback);
  // Walk through all remaining tracees and send out their buffers.
  for (auto it = tracees_.begin(); it != tracees_.end(); ++it) {
    it->Stop(
        [ weak = weak_ptr_factory_.GetWeakPtr(), bundle = it->bundle() ]() {
          if (weak) {
            weak->FinishProvider(bundle);
            weak->FinishSessionIfEmpty();
          }
        });
  }

  session_finalize_timeout_.Start(
      mtl::MessageLoop::GetCurrent()->task_runner().get(),
      [weak = weak_ptr_factory_.GetWeakPtr()]() {
        if (weak)
          weak->done_callback_();
      },
      timeout);
}

void TraceSession::FinishProvider(TraceProviderBundle* bundle) {
  auto it =
      std::find_if(tracees_.begin(), tracees_.end(),
                   [bundle](const auto& tracee) { return tracee == bundle; });

  if (it != tracees_.end()) {
    if (destination_ && !it->WriteRecords(destination_)) {
      // TODO: We should revisit error reporting here and:
      //   - make the return type of Tracee::WriteRecords more expressive
      //   - let calling code know that an unrecoverable error occured.
      FTL_LOG(ERROR) << "Could not write to socket, aborting trace";
      destination_.reset();
    }
    tracees_.erase(it);
  }
}

void TraceSession::FinishSessionIfEmpty() {
  if (tracees_.empty()) {
    session_finalize_timeout_.Stop();
    done_callback_();
  }
}

}  // namespace tracing
