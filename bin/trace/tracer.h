// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TRACER_H_
#define GARNET_BIN_TRACE_TRACER_H_

#include <functional>
#include <string>
#include <vector>

#include <tracing/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/zx/socket.h>
#include <trace-reader/reader.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"

namespace tracing {

// Runs traces.
class Tracer {
 public:
  using RecordConsumer = trace::TraceReader::RecordConsumer;
  using ErrorHandler = trace::TraceReader::ErrorHandler;

  explicit Tracer(TraceController* controller);
  ~Tracer();

  // Starts tracing.
  // Streams records |record_consumer| and errors to |error_handler|.
  // Invokes |done_callback| when tracing stops.
  void Start(TraceOptions options,
             RecordConsumer record_consumer,
             ErrorHandler error_handler,
             fxl::Closure start_callback,
             fxl::Closure done_callback);

  // Stops the trace.
  // Does nothing if not started or if already stopping.
  void Stop();

 private:
  void OnHandleReady(async_t* async,
                     async::WaitBase* wait,
                     zx_status_t status,
                     const zx_packet_signal_t* signal);
  void OnHandleError(zx_status_t status);

  void DrainSocket(async_t* async);
  void CloseSocket();
  void Done();

  TraceController* const controller_;

  enum class State { kStopped, kStarted, kStopping };

  State state_ = State::kStopped;
  fxl::Closure start_callback_;
  fxl::Closure done_callback_;
  zx::socket socket_;
  async_t* async_;
  async::WaitMethod<Tracer, &Tracer::OnHandleReady> wait_;
  std::unique_ptr<trace::TraceReader> reader_;
  std::vector<uint8_t> buffer_;
  size_t buffer_end_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(Tracer);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_TRACER_H_
