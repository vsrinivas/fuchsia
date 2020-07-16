// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage.h"

#include <fbl/algorithm.h>

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

std::pair<int64_t, uint32_t> AlignBufferRequest(int64_t frame, uint32_t length,
                                                uint32_t alignment) {
  uint64_t mask = ~(static_cast<uint64_t>(alignment) - 1);
  int64_t aligned_frame = frame & mask;
  uint32_t aligned_length = (length + alignment - 1) & mask;
  return {aligned_frame, aligned_length};
}

}  // namespace

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
      volume_curve_(std::move(volume_curve)) {
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
    // TODO(50669): We assume that ReadLock always returns exactly the frames we request. This is
    // not true in general, but in practice it's true because source_ is always a MixStage that
    // outputs to an IntermediateBuffer.
    FX_DCHECK(source_buffer->start().Floor() == aligned_first_frame);
    FX_DCHECK(source_buffer->length().Floor() == aligned_frame_count);

    fuchsia_audio_effects_stream_info stream_info;
    stream_info.usage_mask = source_buffer->usage_mask().mask() & kSupportedUsageMask;
    stream_info.gain_dbfs = source_buffer->gain_db();
    stream_info.volume = volume_curve_.DbToVolume(source_buffer->gain_db());
    effects_processor_->SetStreamInfo(stream_info);

    float* buf_out = nullptr;
    auto payload = static_cast<float*>(source_buffer->payload());
    effects_processor_->Process(aligned_frame_count, payload, &buf_out);

    // If the processor has done in-place processing, we want to retain |source_buffer| until we
    // no longer need the frames. If the processor has made a copy then we can release our
    // |source_buffer| since we have a copy in a buffer managed by the effect chain.
    //
    // This buffer will be valid until the next call to |effects_processor_->Process|.
    if (buf_out == payload) {
      current_block_ = std::move(source_buffer);
    } else {
      current_block_ = ReadableStream::Buffer(
          aligned_first_frame, aligned_frame_count, buf_out, source_buffer->is_continuous(),
          source_buffer->usage_mask(), source_buffer->gain_db());
    }
    return DupCurrentBlock();
  }

  return std::nullopt;
}

BaseStream::TimelineFunctionSnapshot EffectsStage::ReferenceClockToFractionalFrames() const {
  auto snapshot = source_->ReferenceClockToFractionalFrames();

  // Update our timeline function to include the latency introduced by these effects.
  //
  // Our effects shift incoming audio into the future by "delay_frames".
  // So input frame[N] corresponds to output frame[N + delay_frames].
  int64_t delay_frames = effects_processor_->delay_frames();
  auto delay_frac_frames = FractionalFrames<int64_t>(delay_frames);

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
