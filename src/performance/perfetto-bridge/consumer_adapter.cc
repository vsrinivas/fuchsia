// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/perfetto-bridge/consumer_adapter.h"

#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <unistd.h>
#include <zircon/status.h>

#include <algorithm>
#include <functional>

#include "lib/trace-engine/context.h"
#include "lib/trace-engine/instrumentation.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "protos/perfetto/config/track_event/track_event_config.gen.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/perfetto/protos/perfetto/common/trace_stats.gen.h"
#include "third_party/perfetto/protos/perfetto/config/data_source_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/trace_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/track_event/track_event_config.gen.h"

namespace {
// The size of the consumer buffer.
constexpr size_t kConsumerBufferSizeKb = 20ul * 1024ul;  // 20MB.

// The time to wait between consumer buffer reads, in milliseconds.
constexpr int kConsumerReadIntervalMs = 2000;

// Interval for recreating interned string data, in milliseconds.
// Used for stream recovery in the event of data loss.
constexpr uint32_t kIncrementalStateClearMs = 4000;

constexpr char kBlobName[] = "perfetto-bridge";
}  // namespace

// Prolongs the lifetime of a trace session when set.
// May be created and freed on any thread.
class ConsumerAdapter::ScopedProlongedTraceContext {
 public:
  ScopedProlongedTraceContext() = default;
  ~ScopedProlongedTraceContext() {
    if (trace_context_)
      trace_release_prolonged_context(trace_context_);
  }
  ScopedProlongedTraceContext(const ScopedProlongedTraceContext&) = delete;
  void operator=(const ScopedProlongedTraceContext&) = delete;

 private:
  trace_prolonged_context_t* trace_context_ = trace_acquire_prolonged_context();
};

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

ConsumerAdapter::State ConsumerAdapter::GetState() {
  std::lock_guard lock(state_mutex_);
  return state_;
}

void ConsumerAdapter::ChangeState(State new_state) {
  std::lock_guard lock(state_mutex_);
  State old_state = state_;

  bool valid_transition = false;
  switch (new_state) {
    case State::INACTIVE:
      valid_transition = old_state == State::SHUTDOWN_STATS;
      break;
    case State::ACTIVE:
      valid_transition = old_state == State::INACTIVE || old_state == State::READING;
      break;
    case State::READING:
      valid_transition = old_state == State::ACTIVE;
      break;
    case State::SHUTDOWN_FLUSH:
      valid_transition = old_state == State::ACTIVE || old_state == State::READING_PENDING_SHUTDOWN;
      break;
    case State::READING_PENDING_SHUTDOWN:
      valid_transition = old_state == State::READING;
      break;
    case State::SHUTDOWN_DISABLED:
      valid_transition = old_state == State::SHUTDOWN_FLUSH || old_state == State::ACTIVE;
      break;
    case State::SHUTDOWN_READING:
      valid_transition = old_state == State::SHUTDOWN_DISABLED;
      break;
    case State::SHUTDOWN_STATS:
      valid_transition = old_state == State::SHUTDOWN_READING;
      break;
  }

  FX_CHECK(valid_transition) << "Unknown state transition: " << static_cast<int>(old_state) << "->"
                             << static_cast<int>(new_state);
  state_ = new_state;
}

void ConsumerAdapter::OnTraceData(std::vector<perfetto::TracePacket> packets, bool has_more) {
  FX_DCHECK(GetState() == State::READING || GetState() == State::SHUTDOWN_READING ||
            GetState() == State::READING_PENDING_SHUTDOWN);
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  if (blob_write_context_) {
    // Proto messages must be written as atomic blobs to prevent truncation mid-message
    // if the output buffer is filled.
    std::string proto_str;
    for (auto& cur_packet : packets) {
      auto [preamble_data, preamble_size] = cur_packet.GetProtoPreamble();
      proto_str.assign(preamble_data, preamble_size);

      for (auto& cur_slice : cur_packet.slices()) {
        proto_str.append(reinterpret_cast<const char*>(cur_slice.start), cur_slice.size);
      }

      if (proto_str.size() > TRACE_MAX_BLOB_SIZE) {
        FX_LOGS(WARNING) << "Dropping excessively long Perfetto message (size=" << proto_str.size()
                         << " bytes)";
      } else {
        trace_context_write_blob_record(blob_write_context_, TRACE_BLOB_TYPE_PERFETTO,
                                        &blob_name_ref_, proto_str.data(), proto_str.size());
      }
      proto_str.clear();
    }
  }

  if (!has_more) {
    OnPerfettoReadBuffersComplete();
  }
}

void ConsumerAdapter::OnTraceStateUpdate() {
  switch (trace_state()) {
    case TRACE_STARTED:
      perfetto_task_runner_->PostTask([this]() { OnStartTracing(); });
      return;
    case TRACE_STOPPING:
      perfetto_task_runner_->PostTask([this]() {
        if (GetState() == State::READING) {
          ChangeState(State::READING_PENDING_SHUTDOWN);
        } else {
          CallPerfettoFlush();
        }
      });
      return;
    case TRACE_STOPPED:
      break;
  }
}

void ConsumerAdapter::OnStartTracing() {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  perfetto::TraceConfig trace_config;
  trace_config.mutable_incremental_state_config()->set_clear_period_ms(kIncrementalStateClearMs);

  perfetto::TraceConfig::BufferConfig* buffer_config = trace_config.add_buffers();
  buffer_config->set_size_kb(kConsumerBufferSizeKb);

  // RING_BUFFER is the only FillPolicy suitable for streaming, because DISCARD will enter a
  // bad state in the event of consumer buffer saturation (e.g. if there is a burst of data).
  buffer_config->set_fill_policy(perfetto::TraceConfig::BufferConfig::RING_BUFFER);

  perfetto::TraceConfig::DataSource* data_source_config = trace_config.add_data_sources();
  // The data source name is necessary and hardcoded for now, but it should
  // be sourced from FXT somehow.
  data_source_config->mutable_config()->set_name("org.chromium.trace_event");
  perfetto::protos::gen::TrackEventConfig track_event_config;
  data_source_config->mutable_config()->set_track_event_config_raw(
      track_event_config.SerializeAsString());

  FX_CHECK(!consumer_endpoint_);
  consumer_endpoint_ = perfetto_service_->ConnectConsumer(this, 0);
  consumer_endpoint_->EnableTracing(trace_config);

  // Explicitly manage the lifetime of the Fuchsia tracing session.
  scoped_prolonged_trace_ = std::make_unique<ScopedProlongedTraceContext>();

  ChangeState(State::ACTIVE);
  SchedulePerfettoReadBuffers();
}

void ConsumerAdapter::CallPerfettoDisableTracing() {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  ChangeState(State::SHUTDOWN_DISABLED);
  consumer_endpoint_->DisableTracing();
}

void ConsumerAdapter::SchedulePerfettoReadBuffers() {
  perfetto_task_runner_->PostDelayedTask(
      [this]() {
        if (GetState() == State::ACTIVE) {
          CallPerfettoReadBuffers(false /* shutdown */);
        }
      },
      kConsumerReadIntervalMs);
}

void ConsumerAdapter::CallPerfettoReadBuffers(bool on_shutdown) {
  FX_DCHECK(!blob_write_context_);
  ChangeState(on_shutdown ? State::SHUTDOWN_READING : State::READING);

  blob_write_context_ = trace_acquire_context();
  if (blob_write_context_) {
    trace_context_register_string_literal(blob_write_context_, kBlobName, &blob_name_ref_);
    consumer_endpoint_->ReadBuffers();
  } else {
    // The Fuchsia tracing context is gone, so there is nowhere to write
    // the data to.
    OnPerfettoReadBuffersComplete();
  }
}

void ConsumerAdapter::OnPerfettoReadBuffersComplete() {
  if (blob_write_context_) {
    trace_release_context(blob_write_context_);
    blob_write_context_ = nullptr;
  }

  if (GetState() == State::READING) {
    ChangeState(State::ACTIVE);
    SchedulePerfettoReadBuffers();
  } else if (GetState() == State::SHUTDOWN_READING) {
    CallPerfettoGetTraceStats();
  } else if (GetState() == State::READING_PENDING_SHUTDOWN) {
    CallPerfettoFlush();
  }
}

void ConsumerAdapter::CallPerfettoFlush() {
  ChangeState(State::SHUTDOWN_FLUSH);
  consumer_endpoint_->Flush(0, [this](bool success) {
    if (!success) {
      FX_LOGS(WARNING) << "Flush failed.";
    }
    CallPerfettoDisableTracing();
  });
}

void ConsumerAdapter::CallPerfettoGetTraceStats() {
  ChangeState(State::SHUTDOWN_STATS);
  consumer_endpoint_->GetTraceStats();
}

void ConsumerAdapter::OnTracingDisabled(const std::string& error) {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  if (!error.empty()) {
    FX_LOGS(WARNING) << "OnTracingDisabled() reported an error: " << error;
  }

  CallPerfettoReadBuffers(true /* shutdown */);
}

void ConsumerAdapter::OnTraceStats(bool success, const perfetto::TraceStats& stats) {
  ChangeState(State::INACTIVE);

  consumer_endpoint_.reset();
  FX_DCHECK(scoped_prolonged_trace_);
  scoped_prolonged_trace_.reset();
  if (blob_write_context_) {
    trace_release_context(blob_write_context_);
  }

  if (!success) {
    FX_LOGS(WARNING) << "Error requesting trace stats from Perfetto.";
    return;
  }

  const auto& buffer_stats = stats.buffer_stats().front();
  FX_LOGS(INFO) << fxl::StringPrintf(
      "Trace stats: "
      "producers_connected: %u , "
      "data_sources_registered: %u , "
      "tracing_sessions: %u",
      stats.producers_connected(), stats.data_sources_registered(), stats.tracing_sessions());
  FX_LOGS(INFO) << fxl::StringPrintf(
      "Consumer buffer stats :"
      "consumer bytes_written: %lu, "
      "consumer bytes_read: %lu, "
      "consumer bytes_overwritten (lost): %lu, ",
      buffer_stats.bytes_written(), buffer_stats.bytes_read(), buffer_stats.bytes_overwritten());
  if (buffer_stats.bytes_overwritten() > 0) {
    // If too much data was lost, then the consumer buffer should be enlarged
    // and/or the drain interval shortened.
    FX_LOGS(WARNING) << "Perfetto consumer buffer overrun detected.";
  }
}

// Ignored Perfetto Consumer events.
void ConsumerAdapter::OnConnect() {}
void ConsumerAdapter::OnDisconnect() {}
void ConsumerAdapter::OnDetach(bool success) {}
void ConsumerAdapter::OnAttach(bool success, const perfetto::TraceConfig&) {}
void ConsumerAdapter::OnObservableEvents(const perfetto::ObservableEvents&) {}
