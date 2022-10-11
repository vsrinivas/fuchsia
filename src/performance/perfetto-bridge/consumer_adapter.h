// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_
#define SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_

#include <lib/trace/observer.h>

#include <memory>

#include <fbl/unique_fd.h>
#include <perfetto/base/task_runner.h>
#include <perfetto/ext/tracing/core/consumer.h>
#include <perfetto/ext/tracing/core/tracing_service.h>

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
  // Drains the Perfetto consumer buffers after a certain interval.
  void ScheduleRead();

  // Handles fuchsia.tracing events.
  void OnTraceStateUpdate();

  // Called in response to the Fuchsia TRACE_STARTED event.
  void OnStartTracing();

  // Called in response to the Fuchsia TRACE_STOPPING event.
  void OnStopTracing();

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

  // Used for handling FXT events.
  trace::TraceObserver trace_observer_;

  size_t trace_entry_count_ = 0;
  size_t trace_bytes_received_ = 0;
};

#endif  // SRC_PERFORMANCE_PERFETTO_BRIDGE_CONSUMER_ADAPTER_H_
