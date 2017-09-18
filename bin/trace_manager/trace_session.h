// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_

#include <functional>
#include <list>
#include <vector>

#include <zx/socket.h>
#include <zx/vmo.h>

#include "lib/tracing/fidl/trace_provider.fidl.h"
#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/tracee.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/one_shot_timer.h"

namespace tracing {

// TraceSession keeps track of all TraceProvider instances that
// are active for a tracing session.
class TraceSession : public fxl::RefCountedThreadSafe<TraceSession> {
 public:
  // Initializes a new instances that streams results
  // to |destination|. Every provider active in this
  // session is handed |categories| and a vmo of size
  // |trace_buffer_size| when started.
  //
  // |abort_handler| is invoked whenever the session encounters
  // unrecoverable errors that render the session dead.
  explicit TraceSession(zx::socket destination,
                        fidl::Array<fidl::String> categories,
                        size_t trace_buffer_size,
                        fxl::Closure abort_handler);
  // Frees all allocated resources and closes the outgoing
  // connection.
  ~TraceSession();

  // Invokes |callback| when all providers in this session have acknowledged
  // the start request, or after |timeout| has elapsed.
  void WaitForProvidersToStart(fxl::Closure callback, fxl::TimeDelta timeout);

  // Starts |provider| and adds it to this session.
  void AddProvider(TraceProviderBundle* provider);
  // Stops |provider|, streaming out all of its trace records.
  void RemoveDeadProvider(TraceProviderBundle* provider);
  // Stops all providers that are part of this session, streams out
  // all remaining trace records and finally invokes |done_callback|.
  //
  // If stopping providers takes longer than |timeout|, we forcefully
  // shutdown operations and invoke |done_callback|.
  void Stop(fxl::Closure done_callback, const fxl::TimeDelta& timeout);

 private:
  enum class State { kReady, kStarted, kStopping, kStopped };

  void NotifyStarted();
  void Abort();
  void NotifyProviderStarted();
  void FinishProvider(TraceProviderBundle* bundle);
  void FinishSessionIfEmpty();
  void FinishSessionDueToTimeout();

  void TransitionToState(State state);

  State state_ = State::kReady;
  zx::socket destination_;
  fidl::Array<fidl::String> categories_;
  size_t trace_buffer_size_;
  std::vector<uint8_t> buffer_;
  std::list<std::unique_ptr<Tracee>> tracees_;
  fxl::OneShotTimer session_start_timeout_;
  fxl::OneShotTimer session_finalize_timeout_;
  fxl::Closure start_callback_;
  fxl::Closure done_callback_;
  fxl::Closure abort_handler_;

  fxl::WeakPtrFactory<TraceSession> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TraceSession);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACE_SESSION_H_
