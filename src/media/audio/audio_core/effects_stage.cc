// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage.h"

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/threading_model.h"
#include "src/media/audio/lib/effects_loader/effects_loader.h"
#include "src/media/audio/lib/effects_loader/effects_processor.h"

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
  Effect CreateEffectByName(std::string_view lib_name, std::string_view effect_name,
                            std::string_view instance_name, uint32_t frame_rate,
                            uint16_t channels_in, uint16_t channels_out, std::string_view config) {
    auto it = std::find_if(holders_.begin(), holders_.end(),
                           [lib_name](auto& holder) { return holder.lib_name == lib_name; });
    if (it == holders_.end()) {
      Holder holder;
      holder.lib_name = lib_name;
      zx_status_t status = EffectsLoader::CreateWithModule(holder.lib_name.c_str(), &holder.loader);
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
    std::unique_ptr<EffectsLoader> loader;
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

std::pair<int64_t, uint32_t> AlignBufferRequest(int64_t frame, uint32_t length,
                                                uint32_t alignment) {
  return {RoundDown(frame, alignment), RoundUp(length, alignment)};
}

}  // namespace

EffectsStage::RingoutBuffer EffectsStage::RingoutBuffer::Create(const Format& format,
                                                                const EffectsProcessor& processor) {
  return RingoutBuffer::Create(format, processor.ring_out_frames(), processor.max_batch_size(),
                               processor.block_size());
}

EffectsStage::RingoutBuffer EffectsStage::RingoutBuffer::Create(const Format& format,
                                                                uint32_t ringout_frames,
                                                                uint32_t max_batch_size,
                                                                uint32_t block_size) {
  uint32_t buffer_frames = 0;
  std::vector<float> buffer;
  if (ringout_frames) {
    // Target our ringout buffer as no larger than a single mix job of frames.
    const uint32_t kTargetRingoutBufferFrames =
        format.frames_per_ns().Scale(ThreadingModel::kMixProfilePeriod.to_nsecs());

    // If the ringout frames is less than our target buffer size, we'll lower it to our ringout
    // frames. Also ensure we do not exceed the max batch size for the effect.
    buffer_frames = std::min(ringout_frames, kTargetRingoutBufferFrames);
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
std::shared_ptr<EffectsStage> EffectsStage::Create(
    const std::vector<PipelineConfig::Effect>& effects, std::shared_ptr<ReadableStream> source,
    VolumeCurve volume_curve) {
  TRACE_DURATION("audio", "EffectsStage::Create");
  if (source->format().sample_format() != fuchsia::media::AudioSampleFormat::FLOAT) {
    FX_LOGS(ERROR) << "EffectsStage can only be added to streams with FLOAT samples";
    return nullptr;
  }

  auto processor = std::make_unique<EffectsProcessor>();

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

  return std::make_shared<EffectsStage>(std::move(source), std::move(processor),
                                        std::move(volume_curve));
}

Format ComputeFormat(const Format& source_format, const EffectsProcessor& processor) {
  return Format::Create(fuchsia::media::AudioStreamType{
                            .sample_format = source_format.sample_format(),
                            .channels = processor.channels_out(),
                            .frames_per_second = source_format.frames_per_second(),
                        })
      .take_value();
}

EffectsStage::EffectsStage(std::shared_ptr<ReadableStream> source,
                           std::unique_ptr<EffectsProcessor> effects_processor,
                           VolumeCurve volume_curve)
    : ReadableStream(ComputeFormat(source->format(), *effects_processor)),
      source_(std::move(source)),
      effects_processor_(std::move(effects_processor)),
      volume_curve_(std::move(volume_curve)),
      ringout_(RingoutBuffer::Create(source_->format(), *effects_processor_)) {
  // Initialize our lead time. Setting 0 here will resolve our lead time to effect delay in our
  // |SetMinLeadTime| override.
  SetMinLeadTime(zx::duration(0));
}

std::optional<ReadableStream::Buffer> EffectsStage::DupCurrentBlock() {
  // To minimize duplicate work, ReadLock saves the last buffer it got from source_->ReadBlock().
  // We can discard this buffer once the caller tells us that the buffer has been fully consumed.
  return std::make_optional<ReadableStream::Buffer>(
      current_block_->start(), current_block_->length(), current_block_->payload(),
      current_block_->is_continuous(), current_block_->usage_mask(), current_block_->gain_db(),
      [this](bool fully_consumed) {
        if (fully_consumed) {
          current_block_ = std::nullopt;
        }
      });
}

std::optional<ReadableStream::Buffer> EffectsStage::ReadLock(zx::time dest_ref_time, int64_t frame,
                                                             uint32_t frame_count) {
  TRACE_DURATION("audio", "EffectsStage::ReadLock", "frame", frame, "length", frame_count);

  // If we have a partially consumed block, return that here.
  if (current_block_) {
    if (frame >= current_block_->start() && frame < current_block_->end()) {
      return DupCurrentBlock();
    }
    // We have a current block that is non-overlapping with this request, so we can release it.
    current_block_ = std::nullopt;
  }

  // New frames are requested. Block-align the start frame and length.
  auto [aligned_first_frame, aligned_frame_count] =
      AlignBufferRequest(frame, frame_count, effects_processor_->block_size());

  // Ensure we don't try to push more frames through our effects processor than supported.
  uint32_t max_batch_size = effects_processor_->max_batch_size();
  if (max_batch_size) {
    aligned_frame_count = std::min<uint32_t>(aligned_frame_count, max_batch_size);
  }

  auto source_buffer = source_->ReadLock(dest_ref_time, aligned_first_frame, aligned_frame_count);
  if (source_buffer) {
    // We expect an integral buffer length.
    FX_CHECK(source_buffer->length().Floor() == source_buffer->length().Ceiling());

    fuchsia_audio_effects_stream_info stream_info;
    stream_info.usage_mask = source_buffer->usage_mask().mask() & kSupportedUsageMask;
    stream_info.gain_dbfs = source_buffer->gain_db();
    stream_info.volume = volume_curve_.DbToVolume(source_buffer->gain_db());
    effects_processor_->SetStreamInfo(stream_info);

    float* buf_out = nullptr;
    auto payload = static_cast<float*>(source_buffer->payload());
    effects_processor_->Process(source_buffer->length().Floor(), payload, &buf_out);

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
      current_block_ = std::move(source_buffer);
    } else {
      current_block_ = ReadableStream::Buffer(
          source_buffer->start(), source_buffer->length(), buf_out, source_buffer->is_continuous(),
          source_buffer->usage_mask(), source_buffer->gain_db());
    }
    return DupCurrentBlock();
  } else if (ringout_frames_sent_ < ringout_.total_frames) {
    if (aligned_first_frame != next_ringout_frame_) {
      FX_LOGS(DEBUG) << "Skipping ringout due to discontinuous buffer";
      ringout_frames_sent_ = ringout_.total_frames;
      return std::nullopt;
    }
    // We have no buffer. If we are still within the ringout period we need to feed some silence
    // into the effects.
    std::fill(ringout_.buffer.begin(), ringout_.buffer.end(), 0.0);
    float* buf_out = nullptr;
    effects_processor_->Process(ringout_.buffer_frames, ringout_.buffer.data(), &buf_out);
    // Ringout frames are by definition continuous with the previous buffer.
    const bool is_continuous = true;
    // TODO(fxbug.dev/50669): Should we clamp length to |frame_count|?
    current_block_ = ReadableStream::Buffer(aligned_first_frame, ringout_.buffer_frames, buf_out,
                                            is_continuous, StreamUsageMask(), 0.0);
    ringout_frames_sent_ += ringout_.buffer_frames;
    next_ringout_frame_ = current_block_->end().Floor();
    return DupCurrentBlock();
  }

  // No buffer and we have no ringout frames remaining, so return silence.
  return std::nullopt;
}

BaseStream::TimelineFunctionSnapshot EffectsStage::ReferenceClockToFixed() const {
  auto snapshot = source_->ReferenceClockToFixed();

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

void EffectsStage::SetMinLeadTime(zx::duration external_lead_time) {
  // Add in any additional lead time required by our effects.
  zx::duration intrinsic_lead_time = ComputeIntrinsicMinLeadTime();
  zx::duration total_lead_time = external_lead_time + intrinsic_lead_time;

  // Apply the total lead time to us and propagate that value to our source.
  ReadableStream::SetMinLeadTime(total_lead_time);
  source_->SetMinLeadTime(total_lead_time);
}

fit::result<void, fuchsia::media::audio::UpdateEffectError> EffectsStage::UpdateEffect(
    const std::string& instance_name, const std::string& config) {
  for (auto& effect : *effects_processor_) {
    if (effect.instance_name() == instance_name) {
      if (effect.UpdateConfiguration(config) == ZX_OK) {
        return fit::ok();
      } else {
        return fit::error(fuchsia::media::audio::UpdateEffectError::INVALID_CONFIG);
      }
    }
  }
  return fit::error(fuchsia::media::audio::UpdateEffectError::NOT_FOUND);
}

zx::duration EffectsStage::ComputeIntrinsicMinLeadTime() const {
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
