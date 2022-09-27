// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/perfetto-bridge/consumer_adapter.h"

#include <fcntl.h>
#include <lib/async/default.h>
#include <unistd.h>

#include <functional>

#include "lib/syslog/cpp/macros.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "src/lib/files/file_descriptor.h"
#include "third_party/perfetto/protos/perfetto/config/data_source_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/trace_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/track_event/track_event_config.gen.h"

namespace {
constexpr int kReadIntervalMs = 1000;
constexpr size_t kConsumerBufferSizeKb = 512;
}  // namespace

ConsumerAdapter::ConsumerAdapter(perfetto::TracingService* perfetto_service,
                                 perfetto::base::TaskRunner* perfetto_task_runner)
    : perfetto_task_runner_(perfetto_task_runner), perfetto_service_(perfetto_service) {
  FX_DCHECK(perfetto_service_);
  FX_DCHECK(perfetto_task_runner_);

  trace_observer_.Start(async_get_default_dispatcher(), [this] { OnTraceStateUpdate(); });
}

ConsumerAdapter::~ConsumerAdapter() {
  perfetto_task_runner_->PostTask(
      [endpoint = this->consumer_endpoint_.release()]() { delete endpoint; });
}

void ConsumerAdapter::OnTracingDisabled(const std::string& error) {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  if (!error.empty()) {
    FX_LOGS(WARNING) << "OnTracingDisabled() reported an error: " << error;
    consumer_endpoint_.reset();
    return;
  }
}

void ConsumerAdapter::OnTraceData(std::vector<perfetto::TracePacket> packets, bool has_more) {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  for (auto& cur_packet : packets) {
    // TODO: Write the preamble from cur_packet.GetProtoPreamble()

    trace_entry_count_ += cur_packet.slices().size();
    for (auto& cur_slice : cur_packet.slices()) {
      trace_bytes_received_ += cur_slice.size;
      // TODO: Write cur_slice.
    }
  }

  if (!has_more) {
    ScheduleRead();
  }
}

void ConsumerAdapter::ScheduleRead() {
  perfetto_task_runner_->PostDelayedTask(
      [this]() {
        if (consumer_endpoint_) {
          consumer_endpoint_->ReadBuffers();
        }
      },
      kReadIntervalMs);
}

void ConsumerAdapter::OnTraceStateUpdate() {
  switch (trace_state()) {
    case TRACE_STARTED:
      perfetto_task_runner_->PostTask([this]() { OnStartTracing(); });
      break;
    case TRACE_STOPPING:
      perfetto_task_runner_->PostTask([this]() { OnStopTracing(); });
      break;
    case TRACE_STOPPED:
      break;
  }
}

void ConsumerAdapter::OnStartTracing() {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  perfetto::TraceConfig trace_config;

  perfetto::TraceConfig::BufferConfig* buffer_config = trace_config.add_buffers();
  buffer_config->set_size_kb(kConsumerBufferSizeKb);
  buffer_config->set_fill_policy(perfetto::TraceConfig::BufferConfig::DISCARD);

  perfetto::TraceConfig::DataSource* data_source_config = trace_config.add_data_sources();
  // The data source name is necessary and hardcoded for now, but it should
  // be sourced from FXT somehow.
  data_source_config->mutable_config()->set_name("org.chromium.trace_event");
  perfetto::protos::gen::TrackEventConfig track_event_config;
  track_event_config.add_enabled_tags("slow");
  track_event_config.add_enabled_tags("debug");
  data_source_config->mutable_config()->set_track_event_config_raw(
      track_event_config.SerializeAsString());

  FX_CHECK(!consumer_endpoint_);
  consumer_endpoint_ = perfetto_service_->ConnectConsumer(this, 0);
  consumer_endpoint_->EnableTracing(trace_config);

  trace_bytes_received_ = 0;
  trace_entry_count_ = 0;
  ScheduleRead();
}

void ConsumerAdapter::OnStopTracing() {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  consumer_endpoint_->DisableTracing();

  // OnDataAvailable() is called synchronously, so the consumer buffers are
  // drained after this point.
  consumer_endpoint_->ReadBuffers();

  consumer_endpoint_.reset();
}

// Ignored Perfetto Consumer events.
void ConsumerAdapter::OnConnect() {}
void ConsumerAdapter::OnDisconnect() {}
void ConsumerAdapter::OnDetach(bool success) {}
void ConsumerAdapter::OnAttach(bool success, const perfetto::TraceConfig&) {}
void ConsumerAdapter::OnTraceStats(bool success, const perfetto::TraceStats&) {}
void ConsumerAdapter::OnObservableEvents(const perfetto::ObservableEvents&) {}
