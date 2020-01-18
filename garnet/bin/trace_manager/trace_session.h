// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>

#include <iosfwd>
#include <list>
#include <vector>

#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/tracee.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr.h"

namespace tracing {

namespace controller = ::fuchsia::tracing::controller;
namespace provider = ::fuchsia::tracing::provider;

// TraceSession keeps track of all TraceProvider instances that
// are active for a tracing session.
class TraceSession : public fxl::RefCountedThreadSafe<TraceSession> {
 public:
  enum class State {
    // The session is ready to be initialized.
    kReady,
    // The session has been initialized.
    kInitialized,
    // The session is starting.
    kStarting,
    // The session is started.
    // We transition to this after all providers have reported started.
    kStarted,
    // The session is being stopped right now.
    kStopping,
    // The session is stopped.
    // We transition to this after all providers have reported stopped.
    kStopped,
    // The session is terminating.
    kTerminating,
    // There is no |kTerminated| state. The session is deleted as part of
    // transitioning to the "terminated" state, and thus is gone (meaning
    // |TraceManager::session_| == nullptr).
  };

  // Initializes a new session that streams results to |destination|.
  // Every provider active in this session is handed |categories| and a vmo of size
  // |buffer_size_megabytes| when started.
  //
  // |abort_handler| is invoked whenever the session encounters
  // unrecoverable errors that render the session dead.
  explicit TraceSession(zx::socket destination, std::vector<std::string> categories,
                        size_t buffer_size_megabytes, provider::BufferingMode buffering_mode,
                        TraceProviderSpecMap&& provider_specs, zx::duration start_timeout,
                        zx::duration stop_timeout, fit::closure abort_handler);

  // Frees all allocated resources and closes the outgoing
  // connection.
  ~TraceSession();

  const zx::socket& destination() const { return destination_; }

  // For testing.
  State state() const { return state_; }

  void set_write_results_on_terminate(bool flag) { write_results_on_terminate_ = flag; }

  // Writes all applicable trace info records.
  // These records are like a pre-amble to the trace, in particular they
  // provide a record at the start of the trace that when written to a file
  // can be used to identify the file as a Fuchsia Trace File.
  void WriteTraceInfo();

  // Initializes |provider| and adds it to this session.
  void AddProvider(TraceProviderBundle* provider);

  // Called after all registered providers have been added.
  void MarkInitialized();

  // Terminates the trace.
  // Stops tracing first if necessary (see |Stop()|).
  // If terminating providers takes longer than |stop_timeout_|, we forcefully
  // terminate tracing and invoke |callback|.
  void Terminate(fit::closure callback);

  // Starts the trace.
  // Invokes |callback| when all providers in this session have
  // acknowledged the start request, or after |start_timeout_| has elapsed.
  void Start(controller::BufferDisposition buffer_disposition,
             const std::vector<std::string>& additional_categories,
             controller::Controller::StartTracingCallback callback);

  // Stops all providers that are part of this session, streams out
  // all remaining trace records and finally invokes |callback|.
  // If |write_results| is true then trace results are written after
  // providers stop (and a flag is set to clear buffer contents if tracing
  // starts again).
  //
  // If stopping providers takes longer than |stop_timeout_|, we forcefully
  // stop tracing and invoke |callback|.
  void Stop(bool write_results, fit::closure callback);

  // Remove |provider|, it's dead Jim.
  void RemoveDeadProvider(TraceProviderBundle* provider);

 private:
  friend std::ostream& operator<<(std::ostream& out, TraceSession::State state);

  // Provider starting processing.
  void OnProviderStarted(TraceProviderBundle* bundle);
  void CheckAllProvidersStarted();
  void NotifyStarted();
  void FinishStartingDueToTimeout();

  // Provider stopping processing.
  void OnProviderStopped(TraceProviderBundle* bundle, bool write_results);
  void CheckAllProvidersStopped();
  void NotifyStopped();
  void FinishStoppingDueToTimeout();

  // Provider termination processing.
  void OnProviderTerminated(TraceProviderBundle* bundle);
  void TerminateSessionIfEmpty();
  void FinishTerminatingDueToTimeout();

  // Timeout handlers.
  void SessionStartTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                           zx_status_t status);
  void SessionStopTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                          zx_status_t status);
  void SessionTerminateTimeout(async_dispatcher_t* dispatcher, async::TaskBase* task,
                               zx_status_t status);

  // Returns true on success or non fatal error.
  // Returns false if a fatal error occurred, in which case the caller is expected to call
  // |Abort()| and immediately return as |this| will be deleted.
  bool WriteProviderData(Tracee* tracee);

  // Abort's the trace session.
  // N.B. Upon return |this| will have been deleted.
  void Abort();

  TransferStatus WriteMagicNumberRecord();

  void TransitionToState(State state);

  State state_ = State::kReady;
  zx::socket destination_;
  fidl::VectorPtr<std::string> categories_;
  size_t buffer_size_megabytes_;
  provider::BufferingMode buffering_mode_;
  TraceProviderSpecMap provider_specs_;
  zx::duration start_timeout_;
  // The stop timeout is used for both stopping and terminating.
  zx::duration stop_timeout_;

  // List of all registered providers (or "tracees"). Note that providers
  // may come and go while tracing is active.
  std::list<std::unique_ptr<Tracee>> tracees_;

  // Saved copy of Start()'s |additional_categories| parameter for tracees that
  // come along after tracing has started.
  std::vector<std::string> additional_categories_;

  async::TaskMethod<TraceSession, &TraceSession::SessionStartTimeout> session_start_timeout_{this};
  async::TaskMethod<TraceSession, &TraceSession::SessionStopTimeout> session_stop_timeout_{this};
  async::TaskMethod<TraceSession, &TraceSession::SessionTerminateTimeout>
      session_terminate_timeout_{this};

  controller::Controller::StartTracingCallback start_callback_;
  fit::closure stop_callback_;
  fit::closure terminate_callback_;

  fit::closure abort_handler_;

  // Force the clearing of provider trace buffers on the next start.
  // This is done when a provider stops with |write_results| set in
  // |StopOptions|.
  bool force_clear_buffer_contents_ = false;

  // If true then write results when the session terminates.
  bool write_results_on_terminate_ = true;

  fxl::WeakPtrFactory<TraceSession> weak_ptr_factory_;

  TraceSession(const TraceSession&) = delete;
  TraceSession(TraceSession&&) = delete;
  TraceSession& operator=(const TraceSession&) = delete;
  TraceSession& operator=(TraceSession&&) = delete;
};

std::ostream& operator<<(std::ostream& out, TraceSession::State state);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_SESSION_H_
