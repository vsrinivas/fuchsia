// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage_v1.h"

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/mix_profile_config.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v1.h"
#include "src/media/audio/lib/effects_loader/effects_processor_v1.h"

namespace media::audio {
namespace {

// We expect our render flags to be the same between StreamUsageMask and the effects bitmask. Both
// are defined as 1u << static_cast<uint32_t>(RenderUsage).
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND)}).mask() ==
              FUCHSIA_AUDIO_EFFECTS_USAGE_BACKGROUND);
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)}).mask() ==
              FUCHSIA_AUDIO_EFFECTS_USAGE_MEDIA);
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)}).mask() ==
              FUCHSIA_AUDIO_EFFECTS_USAGE_INTERRUPTION);
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT)}).mask() ==
              FUCHSIA_AUDIO_EFFECTS_USAGE_SYSTEM_AGENT);
static_assert(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}).mask() ==
              FUCHSIA_AUDIO_EFFECTS_USAGE_COMMUNICATION);
static constexpr uint32_t kSupportedUsageMask =
    StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND),
                     StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                     StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION),
                     StreamUsage::WithRenderUsage(RenderUsage::SYSTEM_AGENT),
                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)})
        .mask();

class MultiLibEffectsLoader {
 public:
  EffectV1 CreateEffectByName(std::string_view lib_name, std::string_view effect_name,
                              std::string_view instance_name, uint32_t frame_rate,
                              uint16_t channels_in, uint16_t channels_out,
                              std::string_view config) {
    auto it = std::find_if(holders_.begin(), holders_.end(),
                           [lib_name](auto& holder) { return holder.lib_name == lib_name; });
    if (it == holders_.end()) {
      Holder holder;
      holder.lib_name = lib_name;
      zx_status_t status =
          EffectsLoaderV1::CreateWithModule(holder.lib_name.c_str(), &holder.loader);
      if (status != ZX_OK) {
        FX_PLOGS(ERROR, status) << "Effect " << effect_name << " from " << lib_name
                                << " unable to be created";
        return {};
      }
      it = holders_.insert(it, std::move(holder));
    }

    FX_CHECK(it != holders_.end());
    return it->loader->CreateEffectByName(effect_name, instance_name, frame_rate, channels_in,
                                          channels_out, config);
  }

 private:
  struct Holder {
    std::string lib_name;
    std::unique_ptr<EffectsLoaderV1> loader;
  };
  std::vector<Holder> holders_;
};

template <typename T>
static constexpr T RoundUp(T t, uint32_t alignment) {
  return fbl::round_up(t, alignment);
}

template <typename T>
static constexpr T RoundDown(T t, uint32_t alignment) {
  using UnsignedType = typename std::make_unsigned<T>::type;
  if (t < 0) {
    return -fbl::round_up(static_cast<UnsignedType>(std::abs(t)), alignment);
  }
  return fbl::round_down(static_cast<UnsignedType>(t), alignment);
}

std::pair<int64_t, uint32_t> AlignBufferRequest(Fixed frame, uint32_t length, uint32_t alignment) {
  return {RoundDown(frame.Floor(), alignment), RoundUp(length, alignment)};
}

}  // namespace

EffectsStageV1::RingoutBuffer EffectsStageV1::RingoutBuffer::Create(
    const Format& format, const EffectsProcessorV1& processor,
    const MixProfileConfig& mix_profile_config) {
  return RingoutBuffer::Create(format, processor.ring_out_frames(), processor.max_batch_size(),
                               processor.block_size(), mix_profile_config.period.to_nsecs());
}

EffectsStageV1::RingoutBuffer EffectsStageV1::RingoutBuffer::Create(
    const Format& format, uint32_t ringout_frames, uint32_t max_batch_size, uint32_t block_size,
    int64_t mix_profile_period_nsecs) {
  uint32_t buffer_frames = 0;
  std::vector<float> buffer;
  if (ringout_frames) {
    // Target our ringout buffer as no larger than a single mix job of frames.
    const uint32_t target_ringout_buffer_frames =
        format.frames_per_ns().Scale(mix_profile_period_nsecs);

    // If the ringout frames is less than our target buffer size, we'll lower it to our ringout
    // frames. Also ensure we do not exceed the max batch size for the effect.
    buffer_frames = std::min(ringout_frames, target_ringout_buffer_frames);
    if (max_batch_size) {
      buffer_frames = std::min(buffer_frames, max_batch_size);
    }

    // Block-align our buffer.
    buffer_frames = RoundUp(buffer_frames, block_size);

    // Allocate the memory to use for the ring-out frames.
    buffer.resize(buffer_frames * format.channels());
  }
  return {
      .total_frames = ringout_frames,
      .buffer_frames = buffer_frames,
      .buffer = std::move(buffer),
  };
}

// static
std::shared_ptr<EffectsStageV1> EffectsStageV1::Create(
    const std::vector<PipelineConfig::EffectV1>& effects, std::shared_ptr<ReadableStream> source,
    const MixProfileConfig& mix_profile_config, VolumeCurve volume_curve) {
  TRACE_DURATION("audio", "EffectsStageV1::Create");
  if (source->format().sample_format() != fuchsia::media::AudioSampleFormat::FLOAT) {
    FX_LOGS(ERROR) << "EffectsStageV1 can only be added to streams with FLOAT samples";
    return nullptr;
  }

  auto processor = std::make_unique<EffectsProcessorV1>();

  MultiLibEffectsLoader loader;
  uint32_t frame_rate = source->format().frames_per_second();
  uint16_t channels_in = source->format().channels();
  for (const auto& effect_spec : effects) {
    uint16_t channels_out = effect_spec.output_channels.value_or(channels_in);
    auto effect = loader.CreateEffectByName(effect_spec.lib_name, effect_spec.effect_name,
                                            effect_spec.instance_name, frame_rate, channels_in,
                                            channels_out, effect_spec.effect_config);
    if (!effect) {
      FX_LOGS(ERROR) << "Unable to create effect '" << effect_spec.effect_name << "' from lib '"
                     << effect_spec.lib_name << "'";
      return nullptr;
    }
    zx_status_t status = processor->AddEffect(std::move(effect));
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Unable to add effect '" << effect_spec.effect_name
                              << "' from lib '" << effect_spec.lib_name << "'";
      return nullptr;
    }
    channels_in = channels_out;
  }

  return std::make_shared<EffectsStageV1>(std::move(source), std::move(processor),
                                          mix_profile_config, std::move(volume_curve));
}

Format ComputeFormat(const Format& source_format, const EffectsProcessorV1& processor) {
  return Format::Create(
             fuchsia::media::AudioStreamType{
                 .sample_format = source_format.sample_format(),
                 .channels = static_cast<uint32_t>(processor.channels_out()),
                 .frames_per_second = static_cast<uint32_t>(source_format.frames_per_second()),
             })
      .take_value();
}

EffectsStageV1::EffectsStageV1(std::shared_ptr<ReadableStream> source,
                               std::unique_ptr<EffectsProcessorV1> effects_processor,
                               const MixProfileConfig& mix_profile_config, VolumeCurve volume_curve)
    : ReadableStream(ComputeFormat(source->format(), *effects_processor)),
      source_(std::move(source)),
      effects_processor_(std::move(effects_processor)),
      volume_curve_(std::move(volume_curve)),
      ringout_(RingoutBuffer::Create(source_->format(), *effects_processor_, mix_profile_config)) {
  // Initialize our lead time. Passing 0 here will resolve to our effect's lead time
  // in our |SetPresentationDelay| override.
  SetPresentationDelay(zx::duration(0));
}

std::optional<ReadableStream::Buffer> EffectsStageV1::ReadLock(ReadLockContext& ctx,
                                                               Fixed dest_frame,
                                                               int64_t frame_count) {
  TRACE_DURATION("audio", "EffectsStageV1::ReadLock", "frame", dest_frame.Floor(), "length",
                 frame_count);

  // If we have a partially consumed block, return that here.
  // Otherwise, the cached block, if any, is no longer needed.
  if (cached_buffer_.Contains(dest_frame)) {
    return cached_buffer_.Get();
  }
  cached_buffer_.Reset();

  // New frames are requested. Block-align the start frame and length.
  auto [aligned_first_frame, aligned_frame_count] =
      AlignBufferRequest(dest_frame, frame_count, effects_processor_->block_size());

  // Ensure we don't try to push more frames through our effects processor than supported.
  uint32_t max_batch_size = effects_processor_->max_batch_size();
  if (max_batch_size) {
    aligned_frame_count = std::min<uint32_t>(aligned_frame_count, max_batch_size);
  }

  auto source_buffer = source_->ReadLock(ctx, Fixed(aligned_first_frame), aligned_frame_count);
  if (source_buffer) {
    fuchsia_audio_effects_stream_info stream_info;
    stream_info.usage_mask = source_buffer->usage_mask().mask() & kSupportedUsageMask;
    stream_info.gain_dbfs = source_buffer->total_applied_gain_db();
    stream_info.volume = volume_curve_.DbToVolume(source_buffer->total_applied_gain_db());
    effects_processor_->SetStreamInfo(stream_info);

    StageMetricsTimer timer("EffectsStageV1::Process");
    timer.Start();

    float* buf_out = nullptr;
    auto payload = static_cast<float*>(source_buffer->payload());
    effects_processor_->Process(source_buffer->length(), payload, &buf_out);

    timer.Stop();
    ctx.AddStageMetrics(timer.Metrics());

    // Since we just sent some frames through the effects, we need to reset our ringout counter if
    // we had one.
    ringout_frames_sent_ = 0;
    next_ringout_frame_ = source_buffer->end().Floor();

    // If the processor has done in-place processing, we want to retain |source_buffer| until we
    // no longer need the frames. If the processor has made a copy then we can release our
    // |source_buffer| since we have a copy in a buffer managed by the effect chain.
    //
    // This buffer will be valid until the next call to |effects_processor_->Process|.
    if (buf_out == payload) {
      cached_buffer_.Set(std::move(*source_buffer));
    } else {
      cached_buffer_.Set(ReadableStream::Buffer(
          source_buffer->start(), source_buffer->length(), buf_out, source_buffer->is_continuous(),
          source_buffer->usage_mask(), source_buffer->total_applied_gain_db()));
    }
    return cached_buffer_.Get();
  } else if (ringout_frames_sent_ < ringout_.total_frames) {
    if (aligned_first_frame != next_ringout_frame_) {
      FX_LOGS(DEBUG) << "Skipping ringout due to discontinuous buffer";
      ringout_frames_sent_ = ringout_.total_frames;
      return std::nullopt;
    }

    StageMetricsTimer timer("EffectsStageV1::Process");
    timer.Start();

    // We have no buffer. If we are still within the ringout period we need to feed some silence
    // into the effects.
    std::fill(ringout_.buffer.begin(), ringout_.buffer.end(), 0.0);
    float* buf_out = nullptr;
    effects_processor_->Process(ringout_.buffer_frames, ringout_.buffer.data(), &buf_out);

    timer.Stop();
    ctx.AddStageMetrics(timer.Metrics());

    // Ringout frames are by definition continuous with the previous buffer.
    const bool is_continuous = true;
    // TODO(fxbug.dev/50669): Should we clamp length to |frame_count|?
    cached_buffer_.Set(ReadableStream::Buffer(Fixed(aligned_first_frame),
                                              static_cast<int64_t>(ringout_.buffer_frames), buf_out,
                                              is_continuous, StreamUsageMask(), 0.0));
    ringout_frames_sent_ += ringout_.buffer_frames;
    next_ringout_frame_ = aligned_first_frame + ringout_.buffer_frames;
    return cached_buffer_.Get();
  }

  // No buffer and we have no ringout frames remaining, so return silence.
  return std::nullopt;
}

BaseStream::TimelineFunctionSnapshot EffectsStageV1::ref_time_to_frac_presentation_frame() const {
  auto snapshot = source_->ref_time_to_frac_presentation_frame();

  // Update our timeline function to include the latency introduced by these effects.
  //
  // Our effects shift incoming audio into the future by "delay_frames".
  // So input frame[N] corresponds to output frame[N + delay_frames].
  int64_t delay_frames = effects_processor_->delay_frames();
  auto delay_frac_frames = Fixed(delay_frames);

  auto source_frac_frame_to_dest_frac_frame =
      TimelineFunction(delay_frac_frames.raw_value(), 0, TimelineRate(1, 1));
  snapshot.timeline_function = source_frac_frame_to_dest_frac_frame * snapshot.timeline_function;

  return snapshot;
}

void EffectsStageV1::SetPresentationDelay(zx::duration external_delay) {
  // Add in any additional lead time required by our effects.
  zx::duration intrinsic_lead_time = ComputeIntrinsicMinLeadTime();
  zx::duration total_delay = external_delay + intrinsic_lead_time;

  if constexpr (kLogPresentationDelay) {
    FX_LOGS(WARNING) << "(" << this << ") " << __FUNCTION__ << " given external_delay "
                     << external_delay.to_nsecs() << "ns";
    FX_LOGS(WARNING) << "Adding it to our intrinsic_lead_time " << intrinsic_lead_time.to_nsecs()
                     << "ns; setting our total_delay " << total_delay.to_nsecs() << "ns";
  }

  // Apply the total lead time to us and propagate that value to our source.
  ReadableStream::SetPresentationDelay(total_delay);
  source_->SetPresentationDelay(total_delay);
}

fpromise::result<void, fuchsia::media::audio::UpdateEffectError> EffectsStageV1::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  for (auto& effect : *effects_processor_) {
    if (effect.instance_name() == instance_name) {
      if (effect.UpdateConfiguration(config) == ZX_OK) {
        return fpromise::ok();
      } else {
        return fpromise::error(fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG);
      }
    }
  }
  return fpromise::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

zx::duration EffectsStageV1::ComputeIntrinsicMinLeadTime() const {
  TimelineRate ticks_per_frame = format().frames_per_ns().Inverse();
  uint32_t lead_frames = effects_processor_->delay_frames();
  uint32_t block_frames = effects_processor_->block_size();
  if (block_frames > 0) {
    // If we have a block size, add up to |block_frames - 1| of additional lead time.
    lead_frames += block_frames - 1;
  }
  return zx::duration(ticks_per_frame.Scale(lead_frames));
}

}  // namespace media::audio
