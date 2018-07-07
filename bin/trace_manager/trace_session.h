// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_

#include <iosfwd>
#include <list>
#include <vector>

#include <fuchsia/tracelink/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/tracee.h"
#include "lib/fidl/cpp/string.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

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
                        fidl::VectorPtr<fidl::StringPtr> categories,
                        size_t trace_buffer_size,
                        fuchsia::tracelink::BufferingMode buffering_mode,
                        fit::closure abort_handler);
  // Frees all allocated resources and closes the outgoing
  // connection.
  ~TraceSession();

  // Invokes |callback| when all providers in this session have acknowledged
  // the start request, or after |timeout| has elapsed.
  void WaitForProvidersToStart(fit::closure callback, zx::duration timeout);

  // Starts |provider| and adds it to this session.
  void AddProvider(TraceProviderBundle* provider);
  // Stops |provider|, streaming out all of its trace records.
  void RemoveDeadProvider(TraceProviderBundle* provider);
  // Stops all providers that are part of this session, streams out
  // all remaining trace records and finally invokes |done_callback|.
  //
  // If stopping providers takes longer than |timeout|, we forcefully
  // shutdown operations and invoke |done_callback|.
  void Stop(fit::closure done_callback, zx::duration timeout);

 private:
  enum class State { kReady, kStarted, kStopping, kStopped };

  friend std::ostream& operator<<(std::ostream& out, TraceSession::State state);

  void NotifyStarted();
  void Abort();
  void CheckAllProvidersStarted();
  void FinishProvider(TraceProviderBundle* bundle);
  void FinishSessionIfEmpty();
  void FinishSessionDueToTimeout();

  void TransitionToState(State state);

  void SessionStartTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                           zx_status_t status);
  void SessionFinalizeTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                              zx_status_t status);

  State state_ = State::kReady;
  zx::socket destination_;
  fidl::VectorPtr<fidl::StringPtr> categories_;
  size_t trace_buffer_size_;
  fuchsia::tracelink::BufferingMode buffering_mode_;
  std::list<std::unique_ptr<Tracee>> tracees_;
  async::TaskMethod<TraceSession, &TraceSession::SessionStartTimeout>
      session_start_timeout_{this};
  async::TaskMethod<TraceSession, &TraceSession::SessionFinalizeTimeout>
      session_finalize_timeout_{this};
  fit::closure start_callback_;
  fit::closure done_callback_;
  fit::closure abort_handler_;

  fxl::WeakPtrFactory<TraceSession> weak_ptr_factory_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TraceSession);
};

std::ostream& operator<<(std::ostream& out, TraceSession::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_
