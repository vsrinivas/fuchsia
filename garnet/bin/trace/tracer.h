// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_TRACER_H_
#define GARNET_BIN_TRACE_TRACER_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/fit/function.h>
#include <lib/trace-engine/fields.h>
#include <lib/zx/socket.h>

#include <array>
#include <string>

#include <trace-reader/reader.h>

namespace tracing {

namespace controller = ::fuchsia::tracing::controller;

// Runs traces.
class Tracer {
 public:
  // These functions are for processing trace records.
  using BytesConsumer = fbl::Function<void(const unsigned char*, size_t)>;
  using RecordConsumer = trace::TraceReader::RecordConsumer;
  using ErrorHandler = trace::TraceReader::ErrorHandler;

  // Called when tracing has completed starting.
  using StartCallback = fit::callback<void(controller::Controller_StartTracing_Result result)>;

  // This is called when there's a failure and trace processing must stop.
  using FailCallback = fit::callback<void()>;

  // This is called on successfully writing trace results.
  // This is not a |fit::callback| as async::PostTask takes a |fit::closure|.
  using DoneCallback = fit::closure;

  // This is called when an alert is received.
  using AlertCallback = fit::function<void(std::string)>;

  explicit Tracer(controller::Controller* controller);
  ~Tracer();

  // Initialize tracing.
  // Streams records |record_consumer| and errors to |error_handler|.
  // Invokes |done_callback| when tracing stops and |fail_callback| upon failure.
  // TODO(fxbug.dev/22974): Remove |binary,record_consumer,error_handler|
  void Initialize(controller::TraceConfig config, bool binary, BytesConsumer bytes_consumer,
                  RecordConsumer record_consumer, ErrorHandler error_handler,
                  FailCallback fail_callback, DoneCallback done_callback,
                  AlertCallback alert_callback);

  // Start tracing.
  // Tracing must not already be started.
  // |start_callback| is called when tracing has fully started.
  void Start(StartCallback start_callback);

  // Terminates the trace.
  // Does nothing if not started or if already terminating.
  void Terminate();

 private:
  enum class State { kReady, kInitialized, kStarted, kTerminating, kTerminated };

  // Note: Buffer needs to be big enough to store records of maximum size.
  static constexpr size_t kReadBufferSize = trace::RecordFields::kMaxRecordSizeBytes * 4;

  void OnHandleReady(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                     const zx_packet_signal_t* signal);
  void OnHandleError(zx_status_t status);

  void DrainSocket(async_dispatcher_t* dispatcher);
  void CloseSocket();
  void Fail();
  void Done();

  void BeginWatchAlert();

  controller::Controller* const controller_;

  State state_ = State::kReady;

  StartCallback start_callback_;
  FailCallback fail_callback_;
  DoneCallback done_callback_;
  AlertCallback alert_callback_;

  zx::socket socket_;
  async_dispatcher_t* dispatcher_;
  async::WaitMethod<Tracer, &Tracer::OnHandleReady> wait_;

  bool binary_;
  BytesConsumer bytes_consumer_;
  std::unique_ptr<trace::TraceReader> reader_;

  // We don't use a vector here to avoid the housekeeping necessary to keep
  // checkers happy (e.g., asan). We use this buffer in an atypical way.
  std::array<uint8_t, kReadBufferSize> buffer_;
  // The amount of space in use in |buffer_|.
  size_t buffer_end_ = 0u;

  Tracer(const Tracer&) = delete;
  Tracer(Tracer&&) = delete;
  Tracer& operator=(const Tracer&) = delete;
  Tracer& operator=(Tracer&&) = delete;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_TRACER_H_
