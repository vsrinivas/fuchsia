// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/input_device_pipeline.h"

#include <lib/syslog/cpp/macros.h>

#include <array>

#include "src/media/audio/audio_core/v2/task_barrier.h"

using ::media::audio::CaptureUsage;
using ::media::audio::DeviceConfig;
using ::media::audio::PipelineConfig;
using ::media::audio::StreamUsage;
using ::media::audio::StreamUsageSet;
using ::media::audio::VolumeCurve;

namespace media_audio {

namespace {

std::pair<int64_t, int64_t> FormatToKey(const Format& format) {
  return std::make_pair(format.channels(), format.frames_per_second());
}

template <typename ResultT>
bool LogResultError(const ResultT& result, const char* debug_context) {
  if (!result.ok()) {
    FX_LOGS(ERROR) << debug_context << ": failed with status " << result;
    return true;
  }
  if (!result->is_ok()) {
    FX_LOGS(ERROR) << debug_context << ": failed with code "
                   << fidl::ToUnderlying(result->error_value());
    return true;
  }
  return false;
}

// An async task that waits for N nodes to be created, then connects those nodes sequentially.
template <uint32_t N>
class Connector : public std::enable_shared_from_this<Connector<N>> {
 public:
  Connector(std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client,
            fit::callback<void()> callback);
  ~Connector();

  void SetNode(uint32_t position, NodeId node);
  void SetFailed();

  // Reports if `SetFailed` has been called.
  bool failed() const { return failed_; }

  // Returns a node in this sequence.
  // REQUIRED: `!failed()`
  NodeId node(uint32_t position) const {
    FX_CHECK(!failed_);
    return *nodes_[position];
  }

 private:
  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client_;
  TaskBarrier barrier_;
  std::array<std::optional<NodeId>, N> nodes_;

  uint32_t remaining_ = N;
  bool failed_ = false;
};

template <uint32_t N>
Connector<N>::Connector(std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client,
                        fit::callback<void()> callback)
    : client_(std::move(client)),
      barrier_([this, callback = std::move(callback)](auto failed) mutable {
        failed_ = failed;
        callback();
      }) {
  // We will create N nodes.
  barrier_.AddPending(N);
}

template <uint32_t N>
Connector<N>::~Connector() {
  if (!failed_) {
    return;
  }
  for (auto& n : nodes_) {
    fidl::Arena arena;
    if (n) {
      (*client_)
          ->DeleteNode(
              fuchsia_audio_mixer::wire::GraphDeleteNodeRequest::Builder(arena).id(*n).Build())
          .Then([](auto&) {});
    }
  }
}

template <uint32_t N>
void Connector<N>::SetNode(uint32_t position, NodeId node) {
  FX_CHECK(remaining_ > 0);
  nodes_[position] = node;

  // After all nodes are created, spawn tasks to create all edges.
  if (--remaining_ == 0) {
    for (uint32_t k = 0; k < N - 1; k++) {
      const auto source = *nodes_[k];
      const auto dest = *nodes_[k + 1];

      barrier_.AddPending();

      fidl::Arena<> arena;
      (*client_)
          ->CreateEdge(fuchsia_audio_mixer::wire::GraphCreateEdgeRequest::Builder(arena)
                           .source_id(source)
                           .dest_id(dest)
                           .Build())
          .Then([this, self = this->shared_from_this()](auto& result) {
            if (LogResultError(result, "CreateEdge")) {
              barrier_.CompleteFailed();
              return;
            }
            barrier_.CompleteSuccess();
          });
    }
  }

  barrier_.CompleteSuccess();
}

template <uint32_t N>
void Connector<N>::SetFailed() {
  barrier_.CompleteFailed();
}

}  // namespace

// static
void InputDevicePipeline::CreateForDevice(DeviceArgs args) {
  FX_CHECK(args.producer.ring_buffer.has_reference_clock());
  FX_CHECK(args.producer.ring_buffer.has_format());
  FX_CHECK(args.producer.ring_buffer.format().has_frames_per_second());
  FX_CHECK(args.producer.ring_buffer.format().frames_per_second() == args.config.rate());

  // State held during this asynchronous operation.
  struct State {
    std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client;
    media::audio::DeviceConfig::InputDeviceProfile config;
    Format format;
    ThreadId thread;
    ReferenceClock reference_clock;
    fit::callback<void(std::shared_ptr<InputDevicePipeline>)> callback;
    std::shared_ptr<Connector<2>> connector;
  };

  auto state = std::make_shared<State>(State{
      .client = std::move(args.graph_client),
      .config = std::move(args.config),
      .format = Format::CreateOrDie(args.producer.ring_buffer.format()),
      .thread = args.thread,
      .reference_clock = ReferenceClock::FromFidlRingBuffer(args.producer.ring_buffer),
      .callback = std::move(args.callback),
  });

  // This callback is invoked after an error or after the edge is created, whichever comes first.
  state->connector = std::make_shared<Connector<2>>(state->client, [state]() {
    if (state->connector->failed()) {
      state->callback(nullptr);
      return;
    }

    auto pipeline = std::shared_ptr<InputDevicePipeline>(new InputDevicePipeline(
        std::move(state->client), state->config.volume_curve(), state->config.supported_usages(),
        state->thread, std::move(state->reference_clock)));

    pipeline->root_splitter_ = state->connector->node(1);
    pipeline->producer_node_ = state->connector->node(0);
    pipeline->splitters_by_format_[FormatToKey(state->format)] = pipeline->root_splitter_;
    pipeline->created_nodes_.insert(state->connector->node(0));
    pipeline->created_nodes_.insert(state->connector->node(1));

    state->callback(std::move(pipeline));
  });

  fidl::Arena arena;

  // Create the producer node.
  (*state->client)
      ->CreateProducer(
          fuchsia_audio_mixer::wire::GraphCreateProducerRequest::Builder(arena)
              .name(args.producer.name)
              .direction(fuchsia_audio_mixer::PipelineDirection::kInput)
              .data_source(fuchsia_audio_mixer::wire::ProducerDataSource::WithRingBuffer(
                  arena, std::move(args.producer.ring_buffer)))
              .external_delay_watcher(std::move(args.producer.external_delay_watcher))
              .Build())
      .Then([state](auto& result) {
        if (LogResultError(result, "CreateProducer")) {
          state->connector->SetFailed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateProducer bug: response missing `id`";
          state->connector->SetFailed();
          return;
        }
        state->connector->SetNode(0, result->value()->id());
      });

  // Create the splitter node.
  (*state->client)
      ->CreateSplitter(fuchsia_audio_mixer::wire::GraphCreateSplitterRequest::Builder(arena)
                           .name(args.producer.name + ".Splitter")
                           .direction(fuchsia_audio_mixer::PipelineDirection::kInput)
                           .format(args.producer.ring_buffer.format())
                           .thread(args.thread)
                           .reference_clock(state->reference_clock.ToFidl(arena))
                           .Build())
      .Then([state](auto& result) {
        if (LogResultError(result, "CreateSplitter")) {
          state->connector->SetFailed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateSplitter bug: response missing `id`";
          state->connector->SetFailed();
          return;
        }
        state->connector->SetNode(1, result->value()->id());
      });
}

// static
std::shared_ptr<InputDevicePipeline> InputDevicePipeline::CreateForLoopback(LoopbackArgs args) {
  auto pipeline = std::shared_ptr<InputDevicePipeline>(
      new InputDevicePipeline(std::move(args.graph_client),
                              VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume),
                              {StreamUsage::WithCaptureUsage(CaptureUsage::LOOPBACK)}, args.thread,
                              std::move(args.reference_clock)));

  pipeline->root_splitter_ = args.splitter_node;
  pipeline->splitters_by_format_[FormatToKey(args.format)] = args.splitter_node;

  return pipeline;
}

void InputDevicePipeline::Start(fidl::AnyArena& arena, fuchsia_media2::wire::RealTime when,
                                fuchsia_media2::wire::StreamTime stream_time) {
  FX_CHECK(*producer_node_);

  // TODO(fxbug.dev/98652): revisit after fixing start/stop semantics in the mixer service
  FX_CHECK(!pending_start_);
  FX_CHECK(!pending_stop_);

  pending_start_ = true;
  (*client_)
      ->Start(fuchsia_audio_mixer::wire::GraphStartRequest::Builder(arena)
                  .node_id(*producer_node_)
                  .when(when)
                  .stream_time(stream_time)
                  .Build())
      .Then([this, self = shared_from_this()](auto& result) {
        pending_start_ = false;
        if (!LogResultError(result, "Start")) {
          started_ = true;
        }
      });
}

void InputDevicePipeline::Stop(fidl::AnyArena& arena, fuchsia_media2::wire::RealOrStreamTime when) {
  FX_CHECK(*producer_node_);

  // TODO(fxbug.dev/98652): revisit after fixing start/stop semantics in the mixer service
  FX_CHECK(!pending_start_);
  FX_CHECK(!pending_stop_);

  pending_stop_ = true;
  (*client_)
      ->Stop(fuchsia_audio_mixer::wire::GraphStopRequest::Builder(arena)
                 .node_id(*producer_node_)
                 .when(when)
                 .Build())
      .Then([this, self = shared_from_this()](auto& result) {
        pending_stop_ = false;
        if (!LogResultError(result, "Start")) {
          started_ = false;
        }
      });
}

void InputDevicePipeline::Destroy() {
  for (auto node : created_nodes_) {
    fidl::Arena arena;
    (*client_)
        ->DeleteNode(
            fuchsia_audio_mixer::wire::GraphDeleteNodeRequest::Builder(arena).id(node).Build())
        .Then([](auto&) {});
  }
}

bool InputDevicePipeline::SupportsUsage(CaptureUsage usage) const {
  return supported_usages_.count(StreamUsage::WithCaptureUsage(usage)) > 0;
}

void InputDevicePipeline::CreateSourceNodeForFormat(
    const Format& desired_format, fit::callback<void(std::optional<NodeId>)> callback) {
  // Check if a suitable source node already exists.
  const auto key = FormatToKey(desired_format);
  if (auto it = splitters_by_format_.find(key); it != splitters_by_format_.end()) {
    callback(it->second);
    return;
  }

  // Create a SplitterNode that uses float32 samples to maximize precision.
  auto format = Format::CreateOrDie({
      .sample_type = fuchsia_audio::SampleType::kFloat32,
      .channels = desired_format.channels(),
      .frames_per_second = desired_format.frames_per_second(),
  });

  fidl::Arena arena;

  // State held during this asynchronous operation.
  struct State {
    Format format;
    fit::callback<void(std::optional<NodeId>)> callback;
    std::shared_ptr<Connector<3>> connector;
  };

  auto state = std::make_shared<State>(State{
      .format = format,
      .callback = std::move(callback),
  });

  // Create a sequence root_splitter -> mixer -> splitter, then return the last splitter.
  state->connector =
      std::make_shared<Connector<3>>(client_, [this, self = shared_from_this(), state]() {
        if (state->connector->failed()) {
          state->callback(std::nullopt);
          return;
        }
        const auto splitter_node = state->connector->node(2);
        splitters_by_format_[FormatToKey(state->format)] = splitter_node;
        state->callback(splitter_node);
      });

  state->connector->SetNode(0, root_splitter_);

  // Create the mixer node.
  (*client_)
      ->CreateMixer(fuchsia_audio_mixer::wire::GraphCreateMixerRequest::Builder(arena)
                        .direction(fuchsia_audio_mixer::PipelineDirection::kInput)
                        .dest_format(format.ToWireFidl(arena))
                        .dest_reference_clock(reference_clock_.ToFidl(arena))
                        .Build())
      .Then([this, self = shared_from_this(), connector = state->connector](auto& result) {
        if (LogResultError(result, "CreateMixer")) {
          connector->SetFailed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateMixer bug: response missing `id`";
          connector->SetFailed();
          return;
        }
        auto id = result->value()->id();
        connector->SetNode(1, id);
        created_nodes_.insert(id);
      });

  // Create the splitter node.
  (*client_)
      ->CreateSplitter(fuchsia_audio_mixer::wire::GraphCreateSplitterRequest::Builder(arena)
                           .direction(fuchsia_audio_mixer::PipelineDirection::kInput)
                           .format(format.ToWireFidl(arena))
                           .thread(thread_)
                           .reference_clock(reference_clock_.ToFidl(arena))
                           .Build())
      .Then([this, self = shared_from_this(), connector = state->connector](auto& result) {
        if (LogResultError(result, "CreateSplitter")) {
          connector->SetFailed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateSplitter bug: response missing `id`";
          connector->SetFailed();
          return;
        }
        auto id = result->value()->id();
        connector->SetNode(2, id);
        created_nodes_.insert(id);
      });
}

}  // namespace media_audio
