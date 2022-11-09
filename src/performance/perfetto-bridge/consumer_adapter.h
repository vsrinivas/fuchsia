// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_
#define SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_

#include <lib/trace-engine/instrumentation.h>
#include <lib/trace/observer.h>

#include <memory>
#include <mutex>

#include <perfetto/base/task_runner.h>
#include <perfetto/ext/tracing/core/consumer.h>
#include <perfetto/ext/tracing/core/tracing_service.h>

#include "lib/trace-engine/context.h"
#include "lib/trace-engine/types.h"
#include "src/lib/fxl/synchronization/thread_annotations.h"

// Adapts the Fuchsia Tracing protocol to the Perfetto Consumer protocol.
// Perfetto events are handled via the perfetto::Consumer method implementations.
// Commands are sent to Perfetto via |consumer_endpoint_|.
class ConsumerAdapter : public perfetto::Consumer {
 public:
  ConsumerAdapter(perfetto::TracingService* perfetto_service,
                  perfetto::base::TaskRunner* perfetto_task_runner);
  ~ConsumerAdapter() override;

  ConsumerAdapter(const ConsumerAdapter& other) = delete;
  void operator=(const ConsumerAdapter& other) = delete;

 private:
  class ScopedProlongedTraceContext;

  // Finite state machine states.
  // State transition rules are applied in ChangeState().
  enum class State {
    INACTIVE,                  // No active trace.
    ACTIVE,                    // Tracing active; waiting for scheduled call to ReadBuffers().
    READING,                   // Scheduled ReadBuffers() called.
    READING_PENDING_SHUTDOWN,  // Shutdown called mid-read. Allows read to finish before Flushing.
    SHUTDOWN_FLUSH,            // Flush() called.
    SHUTDOWN_DISABLED,         // DisableTracing() called.
    SHUTDOWN_READING,          // Final ReadBuffers() called.
    SHUTDOWN_STATS,            // GetTraceStats() called.
  };
  void ChangeState(State new_state);
  State GetState();

  // Handles fuchsia.tracing events.
  void OnTraceStateUpdate();

  // Called in response to the Fuchsia TRACE_STARTED event.
  void OnStartTracing();

  // Called in response to the Fuchsia TRACE_STOPPING event.
  void CallPerfettoDisableTracing();

  void SchedulePerfettoReadBuffers();
  void CallPerfettoReadBuffers(bool on_shutdown);
  void OnPerfettoReadBuffersComplete();

  void CallPerfettoFlush();
  void CallPerfettoGetTraceStats();

  // perfetto::Consumer implementation.
  void OnConnect() override;
  void OnDisconnect() override;
  void OnTracingDisabled(const std::string& error) override;
  void OnTraceData(std::vector<perfetto::TracePacket> packets, bool has_more) override;
  void OnDetach(bool success) override;
  void OnAttach(bool success, const perfetto::TraceConfig&) override;
  void OnTraceStats(bool success, const perfetto::TraceStats&) override;
  void OnObservableEvents(const perfetto::ObservableEvents&) override;

  // Interactions with `perfetto_service_` and `consumer_endpoint_` must take place on
  // `perfetto_task_runner_`. `consumer_endpoint_` lives for the duration of a tracing session.
  perfetto::base::TaskRunner* perfetto_task_runner_;
  perfetto::TracingService* perfetto_service_;
  std::unique_ptr<perfetto::ConsumerEndpoint> consumer_endpoint_;

  std::unique_ptr<ScopedProlongedTraceContext> scoped_prolonged_trace_;
  trace_context_t* blob_write_context_ = nullptr;
  trace_string_ref_t blob_name_ref_;

  // Used for handling FXT events.
  trace::TraceObserver trace_observer_;

  std::atomic<State> state_ FXL_GUARDED_BY(state_mutex_) = State::INACTIVE;
  std::mutex state_mutex_;
};

#endif  // SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_
