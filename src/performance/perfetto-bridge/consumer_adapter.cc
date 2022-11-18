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

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "lib/trace-engine/context.h"
#include "lib/trace-engine/instrumentation.h"
#include "lib/trace-provider/provider.h"
#include "perfetto/ext/tracing/core/trace_packet.h"
#include "perfetto/tracing/core/forward_decls.h"
#include "src/lib/fxl/strings/string_printf.h"
#include "third_party/perfetto/protos/perfetto/common/trace_stats.gen.h"
#include "third_party/perfetto/protos/perfetto/config/chrome/chrome_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/data_source_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/trace_config.gen.h"
#include "third_party/perfetto/protos/perfetto/config/track_event/track_event_config.gen.h"
#include "third_party/rapidjson/include/rapidjson/document.h"

namespace {
// The size of the consumer buffer.
constexpr size_t kConsumerBufferSizeKb = 20ul * 1024ul;  // 20MB.

// The delay between buffer utilization checks.
constexpr int kConsumerStatsPollIntervalMs = 500;

// Sets the amount of buffer usage that will cause the buffer to be read mid-trace.
constexpr float kConsumerUtilizationReadThreshold = 0.6f;

// Interval for recreating interned string data, in milliseconds.
// Used for stream recovery in the event of data loss.
constexpr uint32_t kIncrementalStateClearMs = 4000;

constexpr char kBlobName[] = "perfetto-bridge";

void LogTraceStats(const perfetto::TraceStats& stats) {
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

// TODO(fxbug.dev/115525): Remove this once the migration to track_event_config is complete.
std::string GetChromeTraceConfigString(
    const perfetto::protos::gen::TrackEventConfig& track_event_config) {
  rapidjson::Document chrome_trace_config(rapidjson::kObjectType);
  auto& allocator = chrome_trace_config.GetAllocator();

  rapidjson::Value included_categories(rapidjson::kArrayType);
  for (const auto& enabled_category : track_event_config.enabled_categories()) {
    included_categories.PushBack(rapidjson::StringRef(enabled_category), allocator);
  }
  chrome_trace_config.AddMember("included_categories", included_categories, allocator);

  rapidjson::Value excluded_categories(rapidjson::kArrayType);
  for (const auto& disabled_category : track_event_config.disabled_categories()) {
    excluded_categories.PushBack(rapidjson::StringRef(disabled_category), allocator);
  }
  chrome_trace_config.AddMember("excluded_categories", excluded_categories, allocator);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer writer(buffer);
  chrome_trace_config.Accept(writer);
  return std::string(buffer.GetString(), buffer.GetSize());
}

perfetto::protos::gen::TrackEventConfig GetTrackEventConfig(
    const trace::ProviderConfig& provider_config) {
  perfetto::protos::gen::TrackEventConfig track_event_config;
  if (!provider_config.categories.empty()) {
    // Disable all categories that aren't added to `enabled_categories`
    track_event_config.add_disabled_categories("*");
  }
  for (const auto& enabled_category : provider_config.categories) {
    track_event_config.add_enabled_categories(enabled_category);
  }
  return track_event_config;
}
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
                                 perfetto::base::TaskRunner* perfetto_task_runner,
                                 trace::TraceProviderWithFdio* trace_provider)
    : perfetto_task_runner_(perfetto_task_runner),
      perfetto_service_(perfetto_service),
      trace_provider_(trace_provider) {
  FX_DCHECK(perfetto_service_);
  FX_DCHECK(perfetto_task_runner_);
  FX_DCHECK(trace_provider_);

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
      valid_transition =
          old_state == State::INACTIVE || old_state == State::STATS || old_state == State::READING;
      break;
    case State::STATS:
      valid_transition = old_state == State::ACTIVE;
      break;
    case State::READING:
      valid_transition = old_state == State::STATS;
      break;
    case State::SHUTDOWN_FLUSH:
      valid_transition = old_state == State::ACTIVE ||
                         old_state == State::READING_PENDING_SHUTDOWN || old_state == State::STATS;
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

  perfetto::protos::gen::DataSourceConfig* data_source_config =
      trace_config.add_data_sources()->mutable_config();
  // The data source name is necessary and hardcoded for now, but it should
  // be sourced from FXT somehow.
  data_source_config->set_name("org.chromium.trace_event");

  const auto track_event_config = GetTrackEventConfig(trace_provider_->GetProviderConfig());
  data_source_config->set_track_event_config_raw(track_event_config.SerializeAsString());

  // TODO(fxbug.dev/115525): Remove this once the migration to track_event_config is complete.
  data_source_config->mutable_chrome_config()->set_trace_config(
      GetChromeTraceConfigString(track_event_config));

  FX_CHECK(!consumer_endpoint_);
  consumer_endpoint_ = perfetto_service_->ConnectConsumer(this, 0);
  consumer_endpoint_->EnableTracing(trace_config);

  // Explicitly manage the lifetime of the Fuchsia tracing session.
  scoped_prolonged_trace_ = std::make_unique<ScopedProlongedTraceContext>();

  ChangeState(State::ACTIVE);
  SchedulePerfettoGetStats();
}

void ConsumerAdapter::CallPerfettoDisableTracing() {
  FX_DCHECK(perfetto_task_runner_->RunsTasksOnCurrentThread());

  ChangeState(State::SHUTDOWN_DISABLED);
  consumer_endpoint_->DisableTracing();
}

void ConsumerAdapter::SchedulePerfettoGetStats() {
  FX_DCHECK(GetState() == State::ACTIVE);

  perfetto_task_runner_->PostDelayedTask(
      [this]() {
        if (GetState() == State::ACTIVE) {
          CallPerfettoGetTraceStats(false /* on_shutdown */);
        }
      },
      kConsumerStatsPollIntervalMs);
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
    SchedulePerfettoGetStats();
  } else if (GetState() == State::SHUTDOWN_READING) {
    CallPerfettoGetTraceStats(true);
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

void ConsumerAdapter::CallPerfettoGetTraceStats(bool on_shutdown) {
  ChangeState(on_shutdown ? State::SHUTDOWN_STATS : State::STATS);
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
  if (GetState() == State::STATS) {
    const auto& buffer_stats = stats.buffer_stats().front();
    const size_t buffer_used = buffer_stats.bytes_written() -
                               (buffer_stats.bytes_read() + buffer_stats.bytes_overwritten());
    const float utilization =
        static_cast<float>(buffer_used) / static_cast<float>(buffer_stats.buffer_size());

    if (utilization >= kConsumerUtilizationReadThreshold) {
      CallPerfettoReadBuffers(false /* shutdown */);
    } else {
      ChangeState(State::ACTIVE);
      SchedulePerfettoGetStats();
    }
  } else if (GetState() == State::SHUTDOWN_STATS) {
    ChangeState(State::INACTIVE);

    if (success) {
      LogTraceStats(stats);
    }

    ShutdownTracing();
  }
}

void ConsumerAdapter::ShutdownTracing() {
  consumer_endpoint_.reset();
  FX_DCHECK(scoped_prolonged_trace_);
  scoped_prolonged_trace_.reset();
  if (blob_write_context_) {
    trace_release_context(blob_write_context_);
  }
}

// Ignored Perfetto Consumer events.
void ConsumerAdapter::OnConnect() {}
void ConsumerAdapter::OnDisconnect() {}
void ConsumerAdapter::OnDetach(bool success) {}
void ConsumerAdapter::OnAttach(bool success, const perfetto::TraceConfig&) {}
void ConsumerAdapter::OnObservableEvents(const perfetto::ObservableEvents&) {}
