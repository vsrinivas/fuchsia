// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_TRACER_H_
#define APPS_TRACING_SRC_TRACE_TRACER_H_

#include <mx/socket.h>

#include <functional>
#include <string>
#include <vector>

#include "apps/tracing/lib/trace/reader.h"
#include "apps/tracing/services/trace_controller.fidl.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/tasks/message_loop_handler.h"

namespace tracing {

// Runs traces.
class Tracer : private mtl::MessageLoopHandler {
 public:
  using RecordConsumer = reader::RecordConsumer;
  using ErrorHandler = reader::ErrorHandler;

  explicit Tracer(TraceController* controller);
  ~Tracer() override;

  // Starts tracing.
  // Streams records |record_consumer| and errors to |error_handler|.
  // Invokes |done_callback| when tracing stops.
  void Start(TraceOptionsPtr options,
             RecordConsumer record_consumer,
             ErrorHandler error_handler,
             ftl::Closure start_callback,
             ftl::Closure done_callback);

  // Stops the trace.
  // Does nothing if not started or if already stopping.
  void Stop();

 private:
  // |MessageLoopHandler|:
  void OnHandleReady(mx_handle_t handle, mx_signals_t pending) override;

  void DrainSocket();
  void CloseSocket();
  void Done();

  TraceController* const controller_;

  enum class State { kStopped, kStarted, kStopping };

  State state_ = State::kStopped;
  ftl::Closure start_callback_;
  ftl::Closure done_callback_;
  mx::socket socket_;
  mtl::MessageLoop::HandlerKey handler_key_;
  std::unique_ptr<reader::TraceReader> reader_;
  std::vector<uint8_t> buffer_;
  size_t buffer_end_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(Tracer);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_TRACER_H_
