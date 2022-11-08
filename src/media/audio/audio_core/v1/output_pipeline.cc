// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/output_pipeline.h"

#include "src/media/audio/audio_core/shared/usage_settings.h"
#include "src/media/audio/audio_core/v1/effects_stage_v1.h"
#include "src/media/audio/audio_core/v1/effects_stage_v2.h"
#include "src/media/audio/audio_core/v1/ring_buffer.h"
#include "src/media/audio/audio_core/v1/tap_stage.h"

namespace media::audio {

namespace {

std::vector<StreamUsage> UsagesFromRenderUsages(const std::vector<RenderUsage>& render_usages) {
  std::vector<StreamUsage> usages;

  std::transform(render_usages.cbegin(), render_usages.cend(), std::back_inserter(usages),
                 [](auto usage) { return StreamUsage::WithRenderUsage(usage); });
  return usages;
}

const Format FormatForMixGroup(const PipelineConfig::MixGroup& mix_group) {
  return Format::Create(fuchsia::media::AudioStreamType{
                            .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                            .channels = static_cast<uint32_t>(mix_group.output_channels),
                            .frames_per_second = static_cast<uint32_t>(mix_group.output_rate),
                        })
      .take_value();
}

}  // namespace

OutputPipelineImpl::OutputPipelineImpl(const PipelineConfig& config,
                                       const VolumeCurve& volume_curve,
                                       EffectsLoaderV2* effects_loader_v2,
                                       uint32_t max_block_size_frames,
                                       TimelineFunction ref_pts_to_fractional_frame,
                                       std::shared_ptr<Clock> clock, Mixer::Resampler sampler)
    : OutputPipelineImpl(State(config, volume_curve, effects_loader_v2, max_block_size_frames,
                               ref_pts_to_fractional_frame, std::move(clock), sampler)) {}

OutputPipelineImpl::OutputPipelineImpl(State state)
    : OutputPipeline(state.stream->format()), state_(std::move(state)) {}

OutputPipelineImpl::State::State(const PipelineConfig& config, const VolumeCurve& volume_curve,
                                 EffectsLoaderV2* effects_loader_v2, uint32_t max_block_size_frames,
                                 TimelineFunction ref_pts_to_fractional_frame,
                                 std::shared_ptr<Clock> clock, Mixer::Resampler sampler)
    : audio_clock(std::move(clock)) {
  uint32_t usage_mask = 0;
  stream =
      CreateMixStage(config.root(), volume_curve, effects_loader_v2, max_block_size_frames,
                     fbl::MakeRefCounted<VersionedTimelineFunction>(ref_pts_to_fractional_frame),
                     audio_clock, &usage_mask, sampler);
}

std::shared_ptr<Mixer> OutputPipelineImpl::AddInput(std::shared_ptr<ReadableStream> stream,
                                                    const StreamUsage& usage,
                                                    std::optional<float> initial_dest_gain_db,
                                                    Mixer::Resampler sampler_hint) {
  TRACE_DURATION("audio", "OutputPipelineImpl::AddInput", "stream", stream.get());
  state_.streams.emplace_back(stream, usage);
  return LookupStageForUsage(usage).AddInput(std::move(stream), initial_dest_gain_db, sampler_hint);
}

void OutputPipelineImpl::RemoveInput(const ReadableStream& stream) {
  TRACE_DURATION("audio", "OutputPipelineImpl::RemoveInput", "stream", &stream);
  auto it = std::find_if(state_.streams.begin(), state_.streams.end(),
                         [&stream](auto& pair) { return pair.first.get() == &stream; });
  FX_CHECK(it != state_.streams.end());
  LookupStageForUsage(it->second).RemoveInput(stream);
  state_.streams.erase(it);
}

fpromise::result<void, fuchsia::media::audio::UpdateEffectError> OutputPipelineImpl::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  for (auto& effects_stage : state_.effects_stages_v1) {
    auto result = effects_stage->UpdateEffect(instance_name, config);
    if (result.is_error() &&
        result.error() == fuchsia::media::audio::UpdateEffectError::NOT_FOUND) {
      continue;
    }
    return result;
  }
  return fpromise::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

std::shared_ptr<ReadableStream> OutputPipelineImpl::State::CreateMixStage(
    const PipelineConfig::MixGroup& spec, const VolumeCurve& volume_curve,
    EffectsLoaderV2* effects_loader_v2, uint32_t max_block_size_frames,
    fbl::RefPtr<VersionedTimelineFunction> ref_pts_to_fractional_frame,
    std::shared_ptr<Clock> audio_clock, uint32_t* usage_mask, Mixer::Resampler sampler) {
  auto output_format = FormatForMixGroup(spec);

  auto stage =
      std::make_shared<MixStage>(output_format, max_block_size_frames, ref_pts_to_fractional_frame,
                                 audio_clock, spec.min_gain_db, spec.max_gain_db);
  for (const auto& usage : spec.input_streams) {
    auto mask = 1 << static_cast<uint32_t>(usage);
    FX_DCHECK((*usage_mask & mask) == 0);
    *usage_mask |= mask;
  }

  // If we have effects, we should add that stage in now.
  std::shared_ptr<ReadableStream> root = stage;
  if (!spec.effects_v1.empty()) {
    auto effects_stage = EffectsStageV1::Create(spec.effects_v1, root, volume_curve);
    if (effects_stage) {
      effects_stages_v1.push_back(effects_stage);
      root = std::move(effects_stage);
    }
  } else if (spec.effects_v2.has_value()) {
    FX_CHECK(effects_loader_v2) << "PipelineConfig has V2 effects; need EffectsLoaderV2";
    auto config_result =
        effects_loader_v2->GetProcessorConfiguration(spec.effects_v2->instance_name);
    if (!config_result.ok() || config_result->is_error()) {
      auto status = !config_result.ok() ? config_result.status() : config_result->error_value();
      FX_PLOGS(ERROR, status) << "Failed to get config for V2 effect";
    } else if (auto effect = EffectsStageV2::Create(
                   std::move(config_result->value()->processor_configuration), root);
               effect.is_ok()) {
      root = effect.value();
    } else {
      FX_PLOGS(ERROR, effect.error()) << "Failed to create V2 effect";
    }
  }

  // If this is the loopback stage, allocate the loopback ring buffer. Note we want this to be
  // after any effects that may have been applied.
  if (spec.loopback) {
    FX_DCHECK(!loopback) << "Only a single loopback point is allowed.";
    const uint32_t ring_size = output_format.frames_per_second();
    auto endpoints = BaseRingBuffer::AllocateSoftwareBuffer(
        root->format(), ref_pts_to_fractional_frame, audio_clock, ring_size,
        [ref_pts_to_fractional_frame, audio_clock]() {
          // The loopback capture has no presentation delay. Whatever frame is being presented "now"
          // is the latest safe_write_frame;
          auto pts = audio_clock->now();
          return Fixed::FromRaw(ref_pts_to_fractional_frame.get()->Apply(pts.get())).Floor();
        });
    loopback = std::move(endpoints.reader);
    root = std::make_shared<TapStage>(std::move(root), std::move(endpoints.writer));
  }

  mix_stages.emplace_back(stage, UsagesFromRenderUsages(spec.input_streams));
  for (const auto& input : spec.inputs) {
    auto [timeline_function, _] = ref_pts_to_fractional_frame->get();
    // Create a new timeline function to represent the ref_clock_to_frac_frame mapping for this
    // input.
    auto frac_fps = Fixed(input.output_rate).raw_value();
    auto function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
        // Use the same reference point as the result timeline function. This is to ensure that
        // we align frames between intermediate mix stages to integral frame numbers.
        timeline_function.subject_time(), timeline_function.reference_time(),
        TimelineRate(frac_fps, zx::sec(1).to_nsecs())));
    auto substage = CreateMixStage(input, volume_curve, effects_loader_v2, max_block_size_frames,
                                   function, audio_clock, usage_mask, sampler);
    stage->AddInput(substage, std::nullopt, sampler);
  }
  return root;
}

MixStage& OutputPipelineImpl::LookupStageForUsage(const StreamUsage& usage) {
  for (auto& [mix_stage, stage_usages] : state_.mix_stages) {
    for (const auto& stage_usage : stage_usages) {
      if (stage_usage == usage) {
        return *mix_stage;
      }
    }
  }
  FX_CHECK(false) << "No stage for usage " << usage.ToString();
  __UNREACHABLE;
}

}  // namespace media::audio
