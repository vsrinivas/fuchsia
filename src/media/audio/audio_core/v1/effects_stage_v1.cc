// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/effects_stage_v1.h"

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/v1/logging_flags.h"
#include "src/media/audio/audio_core/v1/mixer/intersect.h"
#include "src/media/audio/audio_core/v1/silence_padding_stream.h"
#include "src/media/audio/audio_core/v1/threading_model.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v1.h"
#include "src/media/audio/lib/effects_loader/effects_processor_v1.h"

namespace media::audio {
namespace {

// Maximum frames per preallocated_source_buffer.
// Maximum bytes is 4096 assuming mono with 32bit frames (float).
constexpr int64_t kMaxFramesPerFrameBuffer = 1024;

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

}  // namespace

// static
std::shared_ptr<EffectsStageV1> EffectsStageV1::Create(
    const std::vector<PipelineConfig::EffectV1>& effects, std::shared_ptr<ReadableStream> source,
    VolumeCurve volume_curve) {
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
                                          std::move(volume_curve));
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
                               VolumeCurve volume_curve)
    : ReadableStream("EffectsStageV1", ComputeFormat(source->format(), *effects_processor)),
      source_(SilencePaddingStream::WrapIfNeeded(std::move(source),
                                                 Fixed(effects_processor->ring_out_frames()),
                                                 /*fractional_gaps_round_down=*/false)),
      effects_processor_(std::move(effects_processor)),
      volume_curve_(std::move(volume_curve)),
      block_size_frames_(effects_processor_->block_size()),
      max_batch_size_frames_(
          effects_processor_->max_batch_size() > 0
              ? std::min(effects_processor_->max_batch_size(), kMaxFramesPerFrameBuffer)
              : kMaxFramesPerFrameBuffer),
      source_buffer_(source_->format(), max_batch_size_frames_) {
  // Check constraints.
  if (block_size_frames_ > 0 && max_batch_size_frames_ > 0) {
    FX_CHECK(max_batch_size_frames_ % block_size_frames_ == 0)
        << "Max batch size " << max_batch_size_frames_ << " must be divisible by "
        << block_size_frames_ << "; original max batch size is "
        << effects_processor_->max_batch_size();
  }

  // Initialize our lead time. Passing 0 here will resolve to our effect's lead time
  // in our |SetPresentationDelay| override.
  SetPresentationDelay(zx::duration(0));
}

std::optional<ReadableStream::Buffer> EffectsStageV1::ReadLockImpl(ReadLockContext& ctx,
                                                                   Fixed dest_frame,
                                                                   int64_t frame_count) {
  // ReadLockImpl should not be called until we've Trim'd past the last cached buffer.
  // See comments for ReadableStream::MakeCachedBuffer.
  FX_CHECK(!cache_);

  // EffectsStageV1 always produces data on integrally-aligned frames.
  dest_frame = Fixed(dest_frame.Floor());

  // Advance to our source's next available frame. This is needed when the source stream
  // contains gaps. For example, given a sequence of calls:
  //
  //   ReadLock(ctx, 0, 20)
  //   ReadLock(ctx, 20, 20)
  //
  // If our block size is 30, then at the first call, we will attempt to produce 30 frames
  // starting at frame 0. If the source has data for that range, we'll cache all 30 processed
  // frames and the second ReadLock call will be handled by our cache.
  //
  // However, if the source has no data for the range [0, 30), the first ReadLock call will
  // return std::nullopt. At the second call, we shouldn't ask the source for any frames
  // before frame 30 because we already know that range is empty.
  if (auto next_available = source_->NextAvailableFrame(); next_available) {
    // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
    int64_t frames_to_trim = next_available->Ceiling() - dest_frame.Floor();
    if (frames_to_trim > 0) {
      frame_count -= frames_to_trim;
      dest_frame += Fixed(frames_to_trim);
    }
  }

  while (frame_count > 0) {
    int64_t frames_read_from_source = FillCache(ctx, dest_frame, frame_count);
    if (cache_) {
      FX_CHECK(source_buffer_.length() > 0);
      FX_CHECK(cache_->dest_buffer);
      return MakeCachedBuffer(source_buffer_.start(), source_buffer_.length(), cache_->dest_buffer,
                              cache_->source_usage_mask, cache_->source_total_applied_gain_db);
    }

    // We tried to process an entire block, however the source had no data.
    // If frame_count > max_frames_per_call_, try the next block.
    dest_frame += Fixed(frames_read_from_source);
    frame_count -= frames_read_from_source;
  }

  // The source has no data for the requested range.
  return std::nullopt;
}

void EffectsStageV1::TrimImpl(Fixed dest_frame) {
  // EffectsStageV1 always produces data on integrally-aligned frames.
  dest_frame = Fixed(dest_frame.Floor());

  if (cache_ && dest_frame >= source_buffer_.end()) {
    cache_ = std::nullopt;
  }
  source_->Trim(dest_frame);
}

int64_t EffectsStageV1::FillCache(ReadLockContext& ctx, Fixed dest_frame, int64_t frame_count) {
  static_assert(FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY == 0);
  static_assert(FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY == 0);

  cache_ = std::nullopt;

  source_buffer_.Reset(dest_frame);
  auto source_usage_mask = StreamUsageMask();
  float source_total_applied_gain_db = 0;
  bool has_data = false;

  // The buffer must have a multiple of block_size_frames_ and at most max_batch_size_frames_.
  // The buffer must have at most frame_count frames (ideally it has exactly that many).
  frame_count = static_cast<int64_t>(
      fbl::round_up(static_cast<uint64_t>(frame_count), static_cast<uint64_t>(block_size_frames_)));
  frame_count = std::min(frame_count, max_batch_size_frames_);

  // Read frame_count frames into source_buffer_.
  while (source_buffer_.length() < frame_count) {
    Fixed start = source_buffer_.end();
    int64_t frames_remaining = frame_count - source_buffer_.length();

    auto buf = source_->ReadLock(ctx, start, frames_remaining);
    if (buf) {
      // SampleAndHold: source frame 1.X overlaps dest frame 2.0, so always round up.
      source_buffer_.AppendData(Fixed(buf->start().Ceiling()), buf->length(), buf->payload());
      source_usage_mask.insert_all(buf->usage_mask());
      source_total_applied_gain_db = buf->total_applied_gain_db();
      has_data = true;
    } else {
      source_buffer_.AppendSilence(start, frames_remaining);
    }
  }

  if (block_size_frames_ > 0) {
    FX_CHECK(source_buffer_.length() % block_size_frames_ == 0)
        << "Bad buffer size " << source_buffer_.length() << " must be divisible by "
        << block_size_frames_;
  }

  // If the source had no frames, we don't need to process anything.
  if (!has_data) {
    return frame_count;
  }

  cache_ = Cache{
      .source_usage_mask = source_usage_mask,
      .source_total_applied_gain_db = source_total_applied_gain_db,
  };

  // Process this buffer.
  fuchsia_audio_effects_stream_info stream_info;
  stream_info.usage_mask = source_usage_mask.mask() & kSupportedUsageMask;
  stream_info.gain_dbfs = source_total_applied_gain_db;
  stream_info.volume = volume_curve_.DbToVolume(source_total_applied_gain_db);
  effects_processor_->SetStreamInfo(stream_info);

  StageMetricsTimer timer("EffectsStageV1::Process");
  timer.Start();

  // The transformed output gets written to cache_.dest_buffer.
  // We hold onto these buffers until the current frame advances to source_buffer_.end().
  auto payload = reinterpret_cast<float*>(source_buffer_.payload());
  effects_processor_->Process(source_buffer_.length(), payload, &cache_->dest_buffer);

  timer.Stop();
  ctx.AddStageMetrics(timer.Metrics());

  return frame_count;
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
    FX_LOGS(INFO) << "(" << this << ") " << __FUNCTION__ << " given external_delay "
                  << external_delay.to_nsecs() << "ns";
    FX_LOGS(INFO) << "Adding it to our intrinsic_lead_time " << intrinsic_lead_time.to_nsecs()
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
  // Lead time must be extended to fill at least one complete block.
  if (block_size_frames_ > 0) {
    lead_frames += block_size_frames_ - 1;
  }
  return zx::duration(ticks_per_frame.Scale(lead_frames));
}

}  // namespace media::audio
