// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v2/output_device_pipeline.h"

#include <lib/syslog/cpp/macros.h>

#include "src/media/audio/audio_core/v2/input_device_pipeline.h"
#include "src/media/audio/audio_core/v2/task_barrier.h"
#include "src/media/audio/lib/format2/format.h"

using ::media::audio::DeviceConfig;
using ::media::audio::PipelineConfig;
using ::media::audio::RenderUsage;
using ::media::audio::VolumeCurve;

namespace media_audio {

namespace {

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

// State for an asynchronous OutputDevicePipeline::Create call.
struct StateForCreate {
  // Will become private fields of OutputDevicePipeline.
  std::shared_ptr<fidl::WireSharedClient<fuchsia_audio_mixer::Graph>> client;
  std::shared_ptr<InputDevicePipeline> loopback;
  VolumeCurve volume_curve;
  std::optional<NodeId> consumer_node;
  std::unordered_map<RenderUsage, NodeId> usage_to_dest_node;
  std::unordered_set<NodeId> created_nodes;

  // Temporary state.
  ThreadId thread;
  ReferenceClock reference_clock;
  std::unique_ptr<media::audio::EffectsLoaderV2> effects_loader;
  std::shared_ptr<TaskBarrier> barrier;
};

// Creates a single PipelineConfig::MixGroup. Creation happens asynchronously. `state->barrier` is
// notified on completion.
class MixGroupCreator : public std::enable_shared_from_this<MixGroupCreator> {
 public:
  MixGroupCreator(std::shared_ptr<StateForCreate> state, const PipelineConfig::MixGroup& spec);

  void Create(const PipelineConfig::MixGroup& spec);

  void SetDestNode(NodeId n) {
    dest_node_ = n;
    CreateEdgesIfReady();
  }

 private:
  void CreateEdge(NodeId source, NodeId dest);
  void CreateEdgesIfReady();
  void Failed();

  const std::shared_ptr<StateForCreate> state_;
  const bool needs_custom_node_;
  const bool needs_splitter_node_;

  // Renderers with these usages can be directly connected to this MixGroup.
  const std::vector<RenderUsage> source_usages_;

  // Each MixGroup is a pipeline that looks like:
  //
  // ```
  // {MixGroup1, MixGroup2, ...} -> MixerNode -> CustomNode -> SplitterNode -> dest
  // ```
  //
  // Each MixGroup can have one or more source MixGroups, recursively. The CustomNode and
  // SplitterNode are optional. The `dest` is either the MixerNode of another MixGroup (for
  // recursive groups) or the device's ConsumerNode (for the root group).
  std::vector<std::shared_ptr<MixGroupCreator>> sources_;
  std::optional<NodeId> mixer_node_;
  std::optional<NodeId> custom_node_;
  std::optional<NodeId> splitter_node_;
  std::optional<NodeId> dest_node_;

  enum class Status {
    kWaitingForNodes,  // sending FIDL calls to create all nodes and waiting for the responses
    kHaveNodes,        // all nodes were created successfully
    kFailed,           // failed to create one or more nodes
  };
  Status status_ = Status::kWaitingForNodes;
};

MixGroupCreator::MixGroupCreator(std::shared_ptr<StateForCreate> state,
                                 const PipelineConfig::MixGroup& spec)
    : state_(std::move(state)),
      needs_custom_node_(spec.effects_v2.has_value()),
      needs_splitter_node_(spec.loopback),
      source_usages_(spec.input_streams) {
  FX_CHECK(spec.effects_v1.empty()) << "V1 effects not supported";
}

void MixGroupCreator::Create(const PipelineConfig::MixGroup& spec) {
  fidl::Arena arena;

  state_->barrier->AddPending();

  // Create the source groups.
  for (const auto& source_spec : spec.inputs) {
    auto source = std::make_shared<MixGroupCreator>(state_, source_spec);
    sources_.push_back(source);
    source->Create(source_spec);
  }

  // Our MixerNode always produces float32 data.
  const auto mixer_dest_format = Format::CreateOrDie({
      .sample_type = fuchsia_audio::SampleType::kFloat32,
      .channels = spec.output_channels,
      .frames_per_second = spec.output_rate,
  });

  // Our SplitterNode uses the same format as its source stream. By default, the source is a
  // MixerNode, but this is overridden if this stage uses a CustomNode.
  auto splitter_format = mixer_dest_format;

  // Create the mixer node.
  (*state_->client)
      ->CreateMixer(fuchsia_audio_mixer::wire::GraphCreateMixerRequest::Builder(arena)
                        .name(spec.name)
                        .direction(fuchsia_audio_mixer::PipelineDirection::kOutput)
                        .dest_format(mixer_dest_format.ToWireFidl(arena))
                        .dest_reference_clock(state_->reference_clock.ToFidl(arena))
                        .Build())
      .Then([this, self = shared_from_this()](auto& result) {
        if (LogResultError(result, "CreateMixer")) {
          Failed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateMixer bug: response missing `id`";
          Failed();
          return;
        }
        mixer_node_ = result->value()->id();
        state_->created_nodes.insert(*mixer_node_);
        for (const auto usage : source_usages_) {
          FX_CHECK(state_->usage_to_dest_node.count(usage) == 0)
              << "multiple mixers for usage " << static_cast<int32_t>(usage);
          state_->usage_to_dest_node[usage] = *mixer_node_;
        }
        CreateEdgesIfReady();
      });

  // Create the custom node, if needed.
  if (needs_custom_node_) {
    auto config_result =
        state_->effects_loader->GetProcessorConfiguration(spec.effects_v2->instance_name);
    if (!config_result.ok() || config_result->is_error()) {
      auto status = !config_result.ok() ? config_result.status() : config_result->error_value();
      FX_PLOGS(ERROR, status) << "Failed to get config for V2 effect; skipping";
    } else {
      auto& config = config_result->value()->processor_configuration;
      FX_CHECK(config.has_outputs());
      FX_CHECK(config.outputs().count() == 1);
      FX_CHECK(config.outputs()[0].has_format());
      splitter_format = Format::CreateLegacyOrDie(config.outputs()[0].format());

      (*state_->client)
          ->CreateCustom(fuchsia_audio_mixer::wire::GraphCreateCustomRequest::Builder(arena)
                             .name(spec.name + ".CustomNode")
                             .direction(fuchsia_audio_mixer::PipelineDirection::kOutput)
                             .config(config)
                             .reference_clock(state_->reference_clock.ToFidl(arena))
                             .Build())
          .Then([this, self = shared_from_this()](auto& result) {
            if (LogResultError(result, "CreateCustom")) {
              Failed();
              return;
            }
            if (!result->value()->has_id()) {
              FX_LOGS(ERROR) << "CreateCustom bug: response missing `id`";
              Failed();
              return;
            }
            custom_node_ = result->value()->id();
            state_->created_nodes.insert(*custom_node_);
            CreateEdgesIfReady();
          });
    }
  }

  // Create the splitter node, if needed.
  if (needs_splitter_node_) {
    (*state_->client)
        ->CreateSplitter(fuchsia_audio_mixer::wire::GraphCreateSplitterRequest::Builder(arena)
                             .name(spec.name + ".Loopback")
                             .direction(fuchsia_audio_mixer::PipelineDirection::kOutput)
                             .format(splitter_format.ToWireFidl(arena))
                             .thread(state_->thread)
                             .reference_clock(state_->reference_clock.ToFidl(arena))
                             .Build())
        .Then([this, self = shared_from_this(), splitter_format](auto& result) {
          if (LogResultError(result, "CreateSplitter")) {
            Failed();
            return;
          }
          if (!result->value()->has_id()) {
            FX_LOGS(ERROR) << "CreateSplitter bug: response missing `id`";
            Failed();
            return;
          }
          splitter_node_ = result->value()->id();
          state_->created_nodes.insert(*splitter_node_);
          state_->loopback = InputDevicePipeline::CreateForLoopback({
              .graph_client = state_->client,
              .splitter_node = *splitter_node_,
              .format = splitter_format,
              .reference_clock = state_->reference_clock.Dup(),
              .thread = state_->thread,
          });
          CreateEdgesIfReady();
        });
  }
}

void MixGroupCreator::CreateEdge(NodeId source, NodeId dest) {
  state_->barrier->AddPending();

  fidl::Arena arena;
  (*state_->client)
      ->CreateEdge(fuchsia_audio_mixer::wire::GraphCreateEdgeRequest::Builder(arena)
                       .source_id(source)
                       .dest_id(dest)
                       .Build())
      .Then([this, self = shared_from_this()](auto& result) {
        if (LogResultError(result, "CreateEdge")) {
          state_->barrier->CompleteFailed();
          return;
        }
        state_->barrier->CompleteSuccess();
      });
}

void MixGroupCreator::CreateEdgesIfReady() {
  FX_CHECK(status_ != Status::kHaveNodes);
  if (status_ == Status::kFailed) {
    return;
  }

  const bool have_nodes = dest_node_ && mixer_node_ && (!needs_custom_node_ || custom_node_) &&
                          (!needs_splitter_node_ || splitter_node_);
  if (!have_nodes) {
    return;
  }
  status_ = Status::kHaveNodes;

  if (needs_custom_node_ && needs_splitter_node_) {
    // Mixer -> Custom -> Splitter -> dest
    CreateEdge(*mixer_node_, *custom_node_);
    CreateEdge(*custom_node_, *splitter_node_);
    CreateEdge(*splitter_node_, *dest_node_);

  } else if (needs_custom_node_) {
    // Mixer -> Custom -> dest
    CreateEdge(*mixer_node_, *custom_node_);
    CreateEdge(*custom_node_, *dest_node_);

  } else if (needs_splitter_node_) {
    // Mixer -> Splitter-> dest
    CreateEdge(*mixer_node_, *splitter_node_);
    CreateEdge(*splitter_node_, *dest_node_);

  } else {
    // Mixer -> dest
    CreateEdge(*mixer_node_, *dest_node_);
  }

  for (auto& source : sources_) {
    source->SetDestNode(*mixer_node_);
  }

  // Finish the task added by `Create`. Additional tasks were added by `CreateEdge` as needed.
  state_->barrier->CompleteSuccess();
}

void MixGroupCreator::Failed() {
  FX_CHECK(status_ != Status::kHaveNodes);
  if (status_ == Status::kFailed) {
    return;
  }
  status_ = Status::kFailed;
  state_->barrier->CompleteFailed();
}

}  // namespace

// static
void OutputDevicePipeline::Create(Args args) {
  FX_CHECK(args.consumer.ring_buffer.has_reference_clock());

  auto state = std::make_shared<StateForCreate>(StateForCreate{
      .client = std::move(args.graph_client),
      .volume_curve = args.config.volume_curve(),
      .thread = args.consumer.thread,
      .reference_clock = ReferenceClock::FromFidlRingBuffer(args.consumer.ring_buffer),
      .effects_loader = std::move(args.effects_loader),
  });

  state->barrier = std::make_shared<TaskBarrier>([state, callback = std::move(args.callback)](
                                                     bool failed) mutable {
    if (failed) {
      FX_LOGS(ERROR) << "OutputDevicePipeline::Create failed";
      // On failure, delete all nodes.
      if (state->loopback) {
        state->loopback->Destroy();
      }
      for (auto node : state->created_nodes) {
        fidl::Arena arena;
        (*state->client)
            ->DeleteNode(
                fuchsia_audio_mixer::wire::GraphDeleteNodeRequest::Builder(arena).id(node).Build())
            .Then([](auto&) {});
      }
      callback(nullptr);
      return;
    }

    FX_CHECK(state->consumer_node);
    callback(std::shared_ptr<OutputDevicePipeline>(new OutputDevicePipeline(
        std::move(state->client), std::move(state->loopback), std::move(state->volume_curve),
        *state->consumer_node, std::move(state->usage_to_dest_node),
        std::move(state->created_nodes))));
  });

  // Add the CreateConsumer task.
  // Do this first to ensure the barrier has the correct task count before any task completes.
  state->barrier->AddPending();

  // Create the source mix group.
  const auto& spec = args.config.pipeline_config().root();
  auto source = std::make_shared<MixGroupCreator>(state, spec);
  source->Create(spec);

  // Create the ConsumerNode.
  fidl::Arena arena;
  (*state->client)
      ->CreateConsumer(fuchsia_audio_mixer::wire::GraphCreateConsumerRequest::Builder(arena)
                           .name(args.consumer.name)
                           .direction(fuchsia_audio_mixer::PipelineDirection::kOutput)
                           .data_sink(fuchsia_audio_mixer::wire::ConsumerDataSink::WithRingBuffer(
                               arena, std::move(args.consumer.ring_buffer)))
                           // MixGroups produce float32 samples
                           .source_sample_type(fuchsia_audio::SampleType::kFloat32)
                           .thread(args.consumer.thread)
                           .external_delay_watcher(args.consumer.external_delay_watcher)
                           .Build())
      .Then([state, source](auto& result) {
        if (LogResultError(result, "CreateConsumer")) {
          state->barrier->CompleteFailed();
          return;
        }
        if (!result->value()->has_id()) {
          FX_LOGS(ERROR) << "CreateConsumer bug: response missing `id`";
          state->barrier->CompleteFailed();
          return;
        }
        state->consumer_node = result->value()->id();
        state->created_nodes.insert(*state->consumer_node);
        source->SetDestNode(*state->consumer_node);
        state->barrier->CompleteSuccess();
      });
}

void OutputDevicePipeline::Start(fidl::AnyArena& arena, fuchsia_media2::wire::RealTime when,
                                 fuchsia_media2::wire::StreamTime stream_time) {
  // TODO(fxbug.dev/98652): revisit after fixing start/stop semantics in the mixer service
  FX_CHECK(!pending_start_);
  FX_CHECK(!pending_stop_);

  pending_start_ = true;
  (*client_)
      ->Start(fuchsia_audio_mixer::wire::GraphStartRequest::Builder(arena)
                  .node_id(consumer_node_)
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

void OutputDevicePipeline::Stop(fidl::AnyArena& arena,
                                fuchsia_media2::wire::RealOrStreamTime when) {
  // TODO(fxbug.dev/98652): revisit after fixing start/stop semantics in the mixer service
  FX_CHECK(!pending_start_);
  FX_CHECK(!pending_stop_);

  pending_stop_ = true;
  (*client_)
      ->Stop(fuchsia_audio_mixer::wire::GraphStopRequest::Builder(arena)
                 .node_id(consumer_node_)
                 .when(when)
                 .Build())
      .Then([this, self = shared_from_this()](auto& result) {
        pending_stop_ = false;
        if (!LogResultError(result, "Stop")) {
          started_ = false;
        }
      });
}

void OutputDevicePipeline::Destroy() {
  if (loopback_) {
    loopback_->Destroy();
  }
  for (auto node : created_nodes_) {
    fidl::Arena arena;
    (*client_)
        ->DeleteNode(
            fuchsia_audio_mixer::wire::GraphDeleteNodeRequest::Builder(arena).id(node).Build())
        .Then([](auto&) {});
  }
}

bool OutputDevicePipeline::SupportsUsage(RenderUsage usage) const {
  return usage_to_dest_node_.count(usage) > 0;
}

NodeId OutputDevicePipeline::DestNodeForUsage(RenderUsage usage) const {
  auto it = usage_to_dest_node_.find(usage);
  FX_CHECK(it != usage_to_dest_node_.end());
  return it->second;
}

}  // namespace media_audio
