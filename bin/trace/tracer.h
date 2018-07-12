// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TRACER_H_
#define GARNET_BIN_TRACE_TRACER_H_

#include <string>
#include <vector>

#include <fuchsia/tracing/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/zx/socket.h>
#include <trace-reader/reader.h>

#include "lib/fxl/macros.h"

namespace tracing {

// Runs traces.
class Tracer {
 public:
  using RecordConsumer = trace::TraceReader::RecordConsumer;
  using ErrorHandler = trace::TraceReader::ErrorHandler;

  explicit Tracer(fuchsia::tracing::TraceController* controller);
  ~Tracer();

  // Starts tracing.
  // Streams records |record_consumer| and errors to |error_handler|.
  // Invokes |done_callback| when tracing stops.
  void Start(fuchsia::tracing::TraceOptions options,
             RecordConsumer record_consumer,
             ErrorHandler error_handler,
             fit::closure start_callback,
             fit::closure done_callback);

  // Stops the trace.
  // Does nothing if not started or if already stopping.
  void Stop();

 private:
  void OnHandleReady(async_dispatcher_t* dispatcher,
                     async::WaitBase* wait,
                     zx_status_t status,
                     const zx_packet_signal_t* signal);
  void OnHandleError(zx_status_t status);

  void DrainSocket(async_dispatcher_t* dispatcher);
  void CloseSocket();
  void Done();

  fuchsia::tracing::TraceController* const controller_;

  enum class State { kStopped, kStarted, kStopping };

  State state_ = State::kStopped;
  fit::closure start_callback_;
  fit::closure done_callback_;
  zx::socket socket_;
  async_dispatcher_t* dispatcher_;
  async::WaitMethod<Tracer, &Tracer::OnHandleReady> wait_;
  std::unique_ptr<trace::TraceReader> reader_;
  std::vector<uint8_t> buffer_;
  size_t buffer_end_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(Tracer);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_TRACER_H_
