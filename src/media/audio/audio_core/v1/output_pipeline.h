// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_OUTPUT_PIPELINE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_OUTPUT_PIPELINE_H_

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <vector>

#include <fbl/ref_ptr.h>

#include "src/media/audio/audio_core/v1/clock.h"
#include "src/media/audio/audio_core/v1/effects_stage_v1.h"
#include "src/media/audio/audio_core/v1/mix_stage.h"
#include "src/media/audio/audio_core/v1/pipeline_config.h"
#include "src/media/audio/audio_core/v1/ring_buffer.h"
#include "src/media/audio/audio_core/v1/stream.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"
#include "src/media/audio/audio_core/v1/volume_curve.h"
#include "src/media/audio/lib/timeline/timeline_function.h"

namespace media::audio {

class OutputPipeline : public ReadableStream {
 public:
  explicit OutputPipeline(Format format) : ReadableStream("OutputPipeline", format) {}
  ~OutputPipeline() override = default;

  // Returns a dup of the loopback stream for this pipeline, or nullptr if there is no loopback.
  virtual std::shared_ptr<ReadableRingBuffer> dup_loopback() const = 0;

  // Adds |stream| as an input to be mixed. The given |usage| will indicate where in the pipeline
  // this stream will be routed.
  virtual std::shared_ptr<Mixer> AddInput(
      std::shared_ptr<ReadableStream> stream, const StreamUsage& usage,
      std::optional<float> initial_dest_gain_db = std::nullopt,
      Mixer::Resampler sampler_hint = Mixer::Resampler::Default) = 0;

  // Removes |stream| from the pipeline.
  //
  // It is an error to call |RemoveInput| without exactly one preceeding call to |AddInput| with the
  // same |stream|.
  virtual void RemoveInput(const ReadableStream& stream) = 0;

  // Sets the configuration of all effects with the given instance name.
  virtual fpromise::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) = 0;
};

class OutputPipelineImpl : public OutputPipeline {
 public:
  // Creates an |OutputPipeline| based on the specification in |config|. The pipeline will
  // ultimately produce output frames via |ReadLock| in the |output_format| requested.
  //
  // |max_block_size_frames| is the largest contiguous region that may be returned from
  // |ReadLock|. If a caller requests a frame region of more that |max_block_size_frames|, then
  // the pipeline will truncate this to only |max_block_size_frames| and the caller will have to
  // call |ReadLock| again to mix the subsequent frames.
  //
  // |ref_clock_to_fractional_frame| is a timeline function that will compute the output frame
  // number (in fixed point format with 13 bits of fractional precision) based on a reference
  // timestamp.
  //
  // The |sampler| is optionally used to select the type of sampler to be used when joining
  // mix stages together.
  OutputPipelineImpl(const PipelineConfig& config, const VolumeCurve& volume_curve,
                     EffectsLoaderV2* effects_loader_v2, uint32_t max_block_size_frames,
                     TimelineFunction ref_time_to_frac_presentation_frame,
                     std::shared_ptr<Clock> audio_clock,
                     Mixer::Resampler sampler = Mixer::Resampler::Default);
  ~OutputPipelineImpl() override = default;

  // |media::audio::OutputPipeline|
  std::shared_ptr<ReadableRingBuffer> dup_loopback() const override {
    if (state_.loopback) {
      return state_.loopback->Dup();
    }
    return nullptr;
  }
  std::shared_ptr<Mixer> AddInput(
      std::shared_ptr<ReadableStream> stream, const StreamUsage& usage,
      std::optional<float> initial_dest_gain_db = std::nullopt,
      Mixer::Resampler sampler_hint = Mixer::Resampler::Default) override;
  void RemoveInput(const ReadableStream& stream) override;
  fpromise::result<void, fuchsia::media::audio::UpdateEffectError> UpdateEffect(
      const std::string& instance_name, const std::string& config) override;

  // |media::audio::ReadableStream|
  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    TRACE_DURATION("audio", "OutputPipeline::ref_time_to_frac_presentation_frame");
    FX_DCHECK(state_.stream);
    return state_.stream->ref_time_to_frac_presentation_frame();
  }
  void SetPresentationDelay(zx::duration external_delay) override {
    ReadableStream::SetPresentationDelay(external_delay);
    state_.stream->SetPresentationDelay(external_delay);
  }
  std::shared_ptr<Clock> reference_clock() override { return state_.audio_clock; }

 private:
  // |media::audio::ReadableStream|
  std::optional<ReadableStream::Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                                     int64_t frame_count) override {
    FX_CHECK(state_.stream);
    return ForwardBuffer(state_.stream->ReadLock(ctx, dest_frame, frame_count));
  }
  void TrimImpl(Fixed dest_frame) override {
    FX_CHECK(state_.stream);
    state_.stream->Trim(dest_frame);
  }

  struct State {
    State(const PipelineConfig& config, const VolumeCurve& curve,
          EffectsLoaderV2* effects_loader_v2, uint32_t max_block_size_frames,
          TimelineFunction ref_clock_to_fractional_frame, std::shared_ptr<Clock> clock,
          Mixer::Resampler sampler);

    std::shared_ptr<ReadableStream> CreateMixStage(
        const PipelineConfig::MixGroup& spec, const VolumeCurve& volume_curve,
        EffectsLoaderV2* effects_loader_v2, uint32_t block_size,
        fbl::RefPtr<VersionedTimelineFunction> ref_clock_to_output_frame,
        std::shared_ptr<Clock> clock, uint32_t* usage_mask, Mixer::Resampler sampler);

    std::vector<std::pair<std::shared_ptr<MixStage>, std::vector<StreamUsage>>> mix_stages;
    std::vector<std::shared_ptr<EffectsStageV1>> effects_stages_v1;
    std::vector<std::pair<std::shared_ptr<ReadableStream>, StreamUsage>> streams;

    // This is the root of the mix graph. The other mix stages must be reachable from this node
    // to actually get mixed.
    std::shared_ptr<ReadableStream> stream;

    std::shared_ptr<ReadableRingBuffer> loopback;

    std::shared_ptr<Clock> audio_clock;
  };

  OutputPipelineImpl(State state);

  MixStage& LookupStageForUsage(const StreamUsage& usage);

  State state_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_OUTPUT_PIPELINE_H_
