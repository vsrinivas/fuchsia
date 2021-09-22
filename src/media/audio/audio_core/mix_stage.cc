// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mix_stage.h"

#include <fuchsia/media/audio/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <iomanip>
#include <limits>
#include <memory>

#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/reporter.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio {
namespace {

TimelineFunction ReferenceClockToIntegralFrames(
    TimelineFunction ref_time_to_frac_presentation_frame) {
  TimelineRate frames_per_fractional_frame = TimelineRate(1, Fixed(1).raw_value());
  return TimelineFunction::Compose(TimelineFunction(frames_per_fractional_frame),
                                   ref_time_to_frac_presentation_frame);
}

zx::duration LeadTimeForMixer(const Format& format, const Mixer& mixer) {
  auto delay_frames = mixer.pos_filter_width().Ceiling();
  TimelineRate ticks_per_frame = format.frames_per_ns().Inverse();
  return zx::duration(ticks_per_frame.Scale(delay_frames));
}

// To what extent should jam-synchronizations be logged? Worst-case logging can exceed 100/sec.
// We log each MixStage's first occurrence; for subsequent instances, depending on audio_core's
// logging level, we throttle the logging frequency depending on log_level.
// By default NDEBUG builds are WARNING, and DEBUG builds INFO. To disable jam-sync logging for a
// certain level, set the interval to 0. To disable all jam-sync logging, set kLogJamSyncs to false.
static constexpr bool kLogJamSyncs = true;
static constexpr uint16_t kJamSyncWarningInterval = 200;  // Log 1 of every 200 jam-syncs at WARNING
static constexpr uint16_t kJamSyncInfoInterval = 20;      // Log 1 of every 20 jam-syncs at INFO
static constexpr uint16_t kJamSyncTraceInterval = 1;      // Log all remaining jam-syncs at TRACE

// For now, allow dest position to move backwards by 960 frames before triggering a position reset.
// Rollback can happen because of differences between the MixStage::ReadLock and Mixer::Mix APIs.
// Otherwise, destination position discontinuities generally indicate a Mix that did not complete --
// an underflow of some kind.
// TODO(fxbug.dev/73306): Stop allowing this (change tolerance to 0 and/or remove this altogether),
// once the system correctly moves position only forward.
static constexpr bool kAllowPositionRollback = true;
static constexpr int64_t kDestPosRollbackTolerance = 960;

// Source position errors generally represent only the rate difference between time sources. We
// reconcile clocks upon every ReadLock call, so even with wildly divergent clocks (+1000ppm vs.
// -1000ppm) source position error would be 1/50 of the duration between ReadLock calls. We set a
// limit of 100x that. If source position error exceeds this, we stop rate-adjustment and instead
// 'snap' to the expected pos (referred to as "jam sync"). This surfaces as a discontinuity (if
// jumping backward) or a dropout (if jumping forward), for this source stream only.
//
// For reference, micro-SRC can smoothly eliminates errors of this duration in approx 1 sec (at
// kMicroSrcAdjustmentPpmMax). If adjusting a zx::clock, this will take less than 3 seconds.
static constexpr zx::duration kMaxErrorThresholdDuration = zx::usec(2500);

}  // namespace

MixStage::MixStage(const Format& output_format, uint32_t block_size,
                   TimelineFunction ref_time_to_frac_presentation_frame, AudioClock& audio_clock,
                   std::optional<float> min_gain_db, std::optional<float> max_gain_db)
    : MixStage(output_format, block_size,
               fbl::MakeRefCounted<VersionedTimelineFunction>(ref_time_to_frac_presentation_frame),
               audio_clock, min_gain_db, max_gain_db) {}

MixStage::MixStage(const Format& output_format, uint32_t block_size,
                   fbl::RefPtr<VersionedTimelineFunction> ref_time_to_frac_presentation_frame,
                   AudioClock& audio_clock, std::optional<float> min_gain_db,
                   std::optional<float> max_gain_db)
    : ReadableStream(output_format),
      output_buffer_frames_(block_size),
      output_buffer_(block_size * output_format.channels()),
      output_ref_clock_(audio_clock),
      output_ref_clock_to_fractional_frame_(ref_time_to_frac_presentation_frame),
      gain_limits_{
          .min_gain_db = min_gain_db,
          .max_gain_db = max_gain_db,
      } {
  FX_CHECK(format().sample_format() == fuchsia::media::AudioSampleFormat::FLOAT)
      << "MixStage must output FLOATs; got format = " << static_cast<int>(format().sample_format());
}

std::shared_ptr<Mixer> MixStage::AddInput(std::shared_ptr<ReadableStream> stream,
                                          std::optional<float> initial_dest_gain_db,
                                          Mixer::Resampler resampler_hint) {
  TRACE_DURATION("audio", "MixStage::AddInput");
  if (!stream) {
    FX_LOGS(ERROR) << "Null stream, cannot add";
    return nullptr;
  }

  if (resampler_hint == Mixer::Resampler::Default &&
      AudioClock::SynchronizationNeedsHighQualityResampler(stream->reference_clock(),
                                                           reference_clock())) {
    resampler_hint = Mixer::Resampler::WindowedSinc;
  }

  auto mixer =
      std::shared_ptr<Mixer>(Mixer::Select(stream->format().stream_type(), format().stream_type(),
                                           resampler_hint, gain_limits_)
                                 .release());
  if (!mixer) {
    mixer = std::make_unique<audio::mixer::NoOp>();
  }

  if (initial_dest_gain_db) {
    mixer->bookkeeping().gain.SetDestGain(*initial_dest_gain_db);
  }

  stream->SetPresentationDelay(GetPresentationDelay() + LeadTimeForMixer(stream->format(), *mixer));

  FX_LOGS(DEBUG) << "AddInput "
                 << (stream->reference_clock().is_adjustable() ? "adjustable " : "static ")
                 << (stream->reference_clock().is_device_clock() ? "device" : "client") << " (self "
                 << (reference_clock().is_adjustable() ? "adjustable " : "static ")
                 << (reference_clock().is_device_clock() ? "device)" : "client)");
  {
    std::lock_guard<std::mutex> lock(stream_lock_);
    streams_.emplace_back(StreamHolder{std::move(stream), mixer});
  }
  return mixer;
}

void MixStage::RemoveInput(const ReadableStream& stream) {
  TRACE_DURATION("audio", "MixStage::RemoveInput");
  std::lock_guard<std::mutex> lock(stream_lock_);
  auto it = std::find_if(streams_.begin(), streams_.end(), [stream = &stream](const auto& holder) {
    return holder.stream.get() == stream;
  });

  if (it == streams_.end()) {
    FX_LOGS(ERROR) << "Input not found, cannot remove";
    return;
  }

  FX_LOGS(DEBUG) << "RemoveInput "
                 << (it->stream->reference_clock().is_adjustable() ? "adjustable " : "static ")
                 << (it->stream->reference_clock().is_device_clock() ? "device" : "client")
                 << " (self " << (reference_clock().is_adjustable() ? "adjustable " : "static ")
                 << (reference_clock().is_device_clock() ? "device)" : "client)");

  streams_.erase(it);
}

std::optional<ReadableStream::Buffer> MixStage::ReadLock(Fixed dest_frame, int64_t frame_count) {
  TRACE_DURATION("audio", "MixStage::ReadLock", "frame", dest_frame.Floor(), "length", frame_count);

  // If we have a partially consumed block, return that here.
  // Otherwise, the cached block, if any, is no longer needed.
  if (cached_buffer_.Contains(dest_frame)) {
    return cached_buffer_.Get();
  }
  cached_buffer_.Reset();

  memset(&cur_mix_job_, 0, sizeof(cur_mix_job_));

  auto snapshot = ref_time_to_frac_presentation_frame();

  cur_mix_job_.buf = &output_buffer_[0];
  cur_mix_job_.buf_frames = std::min(static_cast<int64_t>(frame_count), output_buffer_frames_);
  cur_mix_job_.dest_start_frame = dest_frame.Floor();
  cur_mix_job_.dest_ref_clock_to_frac_dest_frame = snapshot.timeline_function;
  cur_mix_job_.total_applied_gain_db = fuchsia::media::audio::MUTED_GAIN_DB;

  // Fill the output buffer with silence.
  ssize_t bytes_to_zero = cur_mix_job_.buf_frames * format().bytes_per_frame();
  std::memset(cur_mix_job_.buf, 0, bytes_to_zero);
  ForEachSource(TaskType::Mix, dest_frame);

  if (cur_mix_job_.total_applied_gain_db <= fuchsia::media::audio::MUTED_GAIN_DB) {
    // Either we mixed no streams, or all the streams mixed were muted. Either way we can just
    // return nullopt to signify we have no audible frames.
    return std::nullopt;
  }

  // Cache the buffer in case it is not fully read by the caller.
  cached_buffer_.Set(ReadableStream::Buffer(
      Fixed(dest_frame.Floor()), Fixed(cur_mix_job_.buf_frames), cur_mix_job_.buf, true,
      cur_mix_job_.usages_mixed, cur_mix_job_.total_applied_gain_db));
  return cached_buffer_.Get();
}

BaseStream::TimelineFunctionSnapshot MixStage::ref_time_to_frac_presentation_frame() const {
  TRACE_DURATION("audio", "MixStage::ref_time_to_frac_presentation_frame");
  auto [timeline_function, generation] = output_ref_clock_to_fractional_frame_->get();
  return {
      .timeline_function = timeline_function,
      .generation = generation,
  };
}

void MixStage::SetPresentationDelay(zx::duration external_delay) {
  TRACE_DURATION("audio", "MixStage::SetPresentationDelay");

  if constexpr (kLogPresentationDelay) {
    FX_LOGS(WARNING) << "    (" << this << ") " << __FUNCTION__ << " given external_delay "
                     << external_delay.to_nsecs() << "ns";
  }

  ReadableStream::SetPresentationDelay(external_delay);

  // Propagate time to our sources.
  std::lock_guard<std::mutex> lock(stream_lock_);
  for (const auto& holder : streams_) {
    FX_DCHECK(holder.stream);
    FX_DCHECK(holder.mixer);

    zx::duration mixer_lead_time = LeadTimeForMixer(holder.stream->format(), *holder.mixer);

    if constexpr (kLogPresentationDelay) {
      FX_LOGS(WARNING) << "Adding LeadTimeForMixer " << mixer_lead_time.to_nsecs()
                       << "ns to external_delay " << external_delay.to_nsecs() << "ns";
      FX_LOGS(WARNING) << "    (" << this << ") " << __FUNCTION__
                       << " setting child stream total delay "
                       << (external_delay + mixer_lead_time).to_nsecs() << "ns";
    }

    holder.stream->SetPresentationDelay(external_delay + mixer_lead_time);
  }
}

void MixStage::Trim(Fixed dest_frame) {
  TRACE_DURATION("audio", "MixStage::Trim", "frame", dest_frame.Floor());
  ForEachSource(TaskType::Trim, dest_frame);
}

void MixStage::ForEachSource(TaskType task_type, Fixed dest_frame) {
  TRACE_DURATION("audio", "MixStage::ForEachSource");

  std::vector<StreamHolder> sources;
  {
    std::lock_guard<std::mutex> lock(stream_lock_);
    for (const auto& holder : streams_) {
      sources.emplace_back(StreamHolder{holder.stream, holder.mixer});
    }
  }

  for (auto& source : sources) {
    if (task_type == TaskType::Mix) {
      auto& source_info = source.mixer->source_info();
      auto& bookkeeping = source.mixer->bookkeeping();
      ReconcileClocksAndSetStepSize(source_info, bookkeeping, *source.stream);
      MixStream(*source.mixer, *source.stream);
    } else {
      auto dest_ref_time = RefTimeAtFracPresentationFrame(dest_frame);
      auto mono_time = reference_clock().MonotonicTimeFromReferenceTime(dest_ref_time);
      auto source_ref_time =
          source.stream->reference_clock().ReferenceTimeFromMonotonicTime(mono_time);
      auto source_frame = source.stream->FracPresentationFrameAtRefTime(source_ref_time);
      source.stream->Trim(source_frame);
    }
  }
}

void MixStage::MixStream(Mixer& mixer, ReadableStream& stream) {
  TRACE_DURATION("audio", "MixStage::MixStream");
  auto& info = mixer.source_info();
  info.frames_produced = 0;

  // If the renderer is currently paused, subject_delta (not just step_size) is zero. This packet
  // may be relevant eventually, but currently it contributes nothing.
  if (!info.dest_frames_to_frac_source_frames.subject_delta()) {
    return;
  }

  // Calculate the first sampling point for the initial job, in source sub-frames. Use timestamps
  // for the first and last dest frames we need, translated into the source (frac_frame) timeline.
  auto source_for_first_mix_job_frame =
      Fixed::FromRaw(info.dest_frames_to_frac_source_frames(cur_mix_job_.dest_start_frame));

  while (true) {
    // At this point we know we need to consume some source data, but we don't yet know how much.
    // Here is how many destination frames we still need to produce, for this mix job.
    FX_DCHECK(cur_mix_job_.buf_frames >= info.frames_produced);
    int64_t dest_frames_left = cur_mix_job_.buf_frames - info.frames_produced;
    if (dest_frames_left == 0) {
      break;
    }

    // Calculate this job's last sampling point.
    Fixed source_frames =
        Fixed::FromRaw(info.dest_frames_to_frac_source_frames.rate().Scale(dest_frames_left)) +
        mixer.pos_filter_width();

    // Try to grab the front of the packet queue (or ring buffer, if capturing).
    auto stream_buffer = stream.ReadLock(source_for_first_mix_job_frame, source_frames.Ceiling());

    // If the queue is empty, then we are done.
    if (!stream_buffer) {
      break;
    }

    // If the packet is discontinuous, reset our mixer's internal filter state.
    if (!stream_buffer->is_continuous()) {
      // Reset any cached state from previous buffer (but not our long-running position state).
      mixer.Reset();
    }

    // If a packet has no frames, there's no need to mix it; it may be skipped.
    if (stream_buffer->end() == stream_buffer->start()) {
      stream_buffer->set_is_fully_consumed(true);
      continue;
    }

    // Now process the packet at the front of the renderer's queue. If the packet has been
    // entirely consumed, pop it off the front and proceed to the next. Otherwise, we are done.
    auto fully_consumed = ProcessMix(mixer, stream, *stream_buffer);
    stream_buffer->set_is_fully_consumed(fully_consumed);

    // If we have mixed enough destination frames, we are done with this mix, regardless of what
    // we should now do with the source packet.
    if (info.frames_produced == cur_mix_job_.buf_frames) {
      break;
    }
    // If we still need to produce more destination data, but could not complete this source
    // packet (we're paused, or the packet is in the future), then we are done.
    if (!fully_consumed) {
      break;
    }

    source_for_first_mix_job_frame = stream_buffer->end();
  }

  // If there was insufficient supply to meet our demand, we may not have mixed enough frames, but
  // we advance our destination frame count as if we did, because time rolls on. Same for source.
  auto& bookkeeping = mixer.bookkeeping();
  info.AdvanceAllPositionsTo(cur_mix_job_.dest_start_frame + cur_mix_job_.buf_frames, bookkeeping);
  cur_mix_job_.accumulate = true;
}

bool MixStage::ProcessMix(Mixer& mixer, ReadableStream& stream,
                          const ReadableStream::Buffer& source_buffer) {
  TRACE_DURATION("audio", "MixStage::ProcessMix");

  // We are only called by MixStream, which has guaranteed these.
  auto& info = mixer.source_info();
  auto& bookkeeping = mixer.bookkeeping();
  FX_DCHECK(cur_mix_job_.buf_frames > 0);
  FX_DCHECK(info.frames_produced < cur_mix_job_.buf_frames);
  FX_DCHECK(info.dest_frames_to_frac_source_frames.subject_delta());

  // At this point we know we need to consume some source data, but we don't yet know how much.
  // Here is how many destination frames we still need to produce, for this mix job.
  int64_t dest_frames_left = cur_mix_job_.buf_frames - info.frames_produced;
  float* buf = cur_mix_job_.buf + (info.frames_produced * format().channels());

  // Determine this job's first and last sampling points, in source sub-frames. Use the next
  // expected source position saved in our long-running position accounting.
  auto source_for_first_mix_job_frame = info.next_source_frame;

  // This represents the last possible source frame we need for this mix. Note that it is 1 subframe
  // short of the source needed for the SUBSEQUENT dest frame, floored to an integral source frame.
  // We cannot just subtract one integral frame from the source corresponding to the next start dest
  // because very large or small step_size values make this 1-frame assumption invalid.
  //
  auto modulo_contribution_to_final_mix_job_frame = Fixed::FromRaw(
      (bookkeeping.source_pos_modulo + bookkeeping.rate_modulo() * dest_frames_left) /
          bookkeeping.denominator() -
      1);
  Fixed source_for_final_mix_job_frame =
      source_for_first_mix_job_frame + (bookkeeping.step_size * dest_frames_left);
  source_for_final_mix_job_frame += modulo_contribution_to_final_mix_job_frame;

  // The above two calculated values characterize our demand. Now reason about our supply.
  //
  // Assert our implementation-defined limit is compatible with the FIDL limit. The latter is
  // already enforced by the renderer implementation.
  static_assert(fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET <= Fixed::Max().Floor());
  FX_DCHECK(source_buffer.end() > source_buffer.start());
  FX_DCHECK(source_buffer.length() <= Fixed(Fixed::Max()));

  // Calculate the actual first and final frame times in the source packet.
  auto source_for_first_packet_frame = source_buffer.start();
  Fixed source_for_final_packet_frame = source_buffer.end() - Fixed(1);

  // If this source packet's final audio frame occurs before our filter's negative edge, centered at
  // our first sampling point, then this packet is entirely in the past and may be skipped.
  // Returning true means we're done with the packet (it can be completed) and we would like another
  auto neg_width = mixer.neg_filter_width();
  if (source_for_final_packet_frame < Fixed(source_for_first_mix_job_frame - neg_width)) {
    auto source_frames_late =
        Fixed(source_for_first_mix_job_frame - neg_width - source_for_first_packet_frame);
    auto clock_mono_late = zx::nsec(info.clock_mono_to_frac_source_frames.rate().Inverse().Scale(
        source_frames_late.raw_value()));

    stream.ReportUnderflow(source_for_first_packet_frame, source_for_first_mix_job_frame,
                           clock_mono_late);
    return true;
  }

  // If this source packet's first audio frame occurs after our filter's positive edge, centered at
  // our final sampling point, then this packet is entirely in the future and should be held.
  // Returning false (based on requirement that packets must be presented in timestamp-chronological
  // order) means that we have consumed all of the available packet "supply" as we can at this time.
  auto pos_width = mixer.pos_filter_width();
  if (source_for_first_packet_frame > Fixed(source_for_final_mix_job_frame + pos_width)) {
    return false;
  }

  // If neither of the above, then evidently this source packet intersects our mixer's filter.
  // Compute the offset into the dest buffer where our first generated sample should land, and the
  // offset into the source packet where we should start sampling.
  int64_t initial_dest_advance = 0;
  Fixed source_offset = source_for_first_mix_job_frame - source_for_first_packet_frame;
  Fixed source_pos_edge_first_mix_frame = source_for_first_mix_job_frame + pos_width;

  // If the packet's first frame comes after the filter window's positive edge,
  // then we should skip some frames in the destination buffer before starting to produce data.
  if (source_for_first_packet_frame > source_pos_edge_first_mix_frame) {
    const TimelineRate& dest_to_frac_source = info.dest_frames_to_frac_source_frames.rate();
    // The dest_buffer offset is based on the distance from mix job start to packet start (measured
    // in frac_frames), converted into frames in the destination timeline. As we scale the source
    // frac_frame delta into dest frames, we want to "round up" any subframes that are present; any
    // source subframes should push our dest frame up to the next integer. Because we entered this
    // IF in the first place, we have a non-zero fractional source delta, thus initial_dest_advance
    // is guaranteed to become greater than zero.
    //
    // When a position is round-trip converted to another timeline and back again, there is no
    // guarantee that it will result in the exact original value. To make source -> dest -> source
    // as accurate as possible (and critically, to ensure that source position does not move
    // backward), we "round up" when translating from source (fractional) to dest (integral).
    auto first_source_mix_point =
        Fixed(source_for_first_packet_frame - source_pos_edge_first_mix_frame);
    initial_dest_advance = dest_to_frac_source.Inverse().Scale(first_source_mix_point.raw_value(),
                                                               TimelineRate::RoundingMode::Ceiling);
    auto previous_source_running_position = info.next_source_frame;
    FX_CHECK(initial_dest_advance > 0);
    info.AdvanceAllPositionsBy(initial_dest_advance, bookkeeping);
    auto new_source_running_position = info.next_source_frame;
    source_offset += new_source_running_position;
    source_offset -= previous_source_running_position;

    // Packet is within the mix window but starts after mix start. MixStream breaks mix jobs into
    // multiple pieces so that each packet gets its own ProcessMix call; this means there was no
    // contiguous packet immediately before this one. For now we don't report this as a problem;
    // eventually (when we can rely on clients to accurately set STREAM_PACKET_FLAG_DISCONTINUITY),
    // we should report this as a minor discontinuity if that flag is NOT set -- via something like
    //    stream.ReportPartialUnderflow(frac_source_offset,dest_offset);
    //
    // TODO(mpuryear): move packet discontinuity (gap/overlap) detection up into the
    // Renderer/PacketQueue, and remove PartialUnderflow reporting and the metric altogether.
  }
  int64_t dest_offset = initial_dest_advance;

  // Looks like we are ready to go. Mix.
  bool consumed_source;
  if (dest_offset >= dest_frames_left) {
    // We initially needed to source frames from this packet in order to finish this mix. After
    // realigning our sampling point to the nearest dest frame, that dest frame is now at or beyond
    // the end of this mix job. We have no need to mix any source material now, so just exit.
    consumed_source = false;
  } else if (source_offset + mixer.pos_filter_width() >= source_buffer.length()) {
    // This packet was initially within our mix window. After realigning our sampling point to the
    // nearest dest frame, it is now entirely in the past. This can only occur when down-sampling
    // and is made more likely if the rate conversion ratio is very high. We've already reported
    // a partial underflow when realigning, so just complete the packet and move on to the next.
    consumed_source = true;
  } else {
    auto prev_dest_offset = dest_offset;
    auto dest_ref_clock_to_integral_dest_frame =
        ReferenceClockToIntegralFrames(cur_mix_job_.dest_ref_clock_to_frac_dest_frame);

    // Check whether we are still ramping
    float local_gain_db;
    bool ramping = bookkeeping.gain.IsRamping();
    if (ramping) {
      auto scale_arr_max = bookkeeping.gain.GetScaleArray(
          bookkeeping.scale_arr.get(),
          std::min(dest_frames_left - dest_offset, Mixer::Bookkeeping::kScaleArrLen),
          dest_ref_clock_to_integral_dest_frame.rate());
      local_gain_db = Gain::ScaleToDb(scale_arr_max);
    } else {
      local_gain_db = bookkeeping.gain.GetGainDb();
    }

    {
      consumed_source =
          mixer.Mix(buf, dest_frames_left, &dest_offset, source_buffer.payload(),
                    source_buffer.length().Floor(), &source_offset, cur_mix_job_.accumulate);
      cur_mix_job_.usages_mixed.insert_all(source_buffer.usage_mask());

      // Total applied gain: previously applied gain, plus any gain added at this stage.
      float total_applied_gain_db =
          Gain::CombineGains(source_buffer.total_applied_gain_db(), local_gain_db);
      // Record the max applied gain of any source stream.
      cur_mix_job_.total_applied_gain_db =
          std::max(cur_mix_job_.total_applied_gain_db, total_applied_gain_db);
    }

    // If source is ramping, advance that ramp by the amount of dest that was just mixed.
    if (ramping) {
      bookkeeping.gain.Advance(dest_offset - prev_dest_offset,
                               dest_ref_clock_to_integral_dest_frame.rate());
    }
  }

  FX_DCHECK(dest_offset <= dest_frames_left);
  // Bookkeeping (including source_pos_modulo) has already advanced -- now update long-running
  // position (without double-incrementing source_pos_modulo).
  info.UpdateRunningPositionsBy(dest_offset - initial_dest_advance, bookkeeping);

  if (consumed_source) {
    FX_DCHECK(source_offset + mixer.pos_filter_width() >= source_buffer.length());
  }

  info.frames_produced += dest_offset;
  FX_DCHECK(info.frames_produced <= cur_mix_job_.buf_frames);

  return consumed_source;
}

// We compose the effects of clock reconciliation into our sample-rate-conversion step size, but
// only for streams that use neither our adjustable clock, nor the clock designated as driving our
// hardware-rate-adjustments. We apply this micro-SRC via an intermediate "slew away the error"
// rate-correction factor driven by a PID control. Why use a PID? Sources do not merely chase the
// other clock's rate -- they chase its position. Note that even if we don't adjust our rate, we
// still want a composed transformation for offsets.
//
// Calculate the composed dest-to-source transformation and update the mixer's bookkeeping for
// step_size etc. These are the only deliverables for this method.
void MixStage::ReconcileClocksAndSetStepSize(Mixer::SourceInfo& info,
                                             Mixer::Bookkeeping& bookkeeping,
                                             ReadableStream& stream) {
  TRACE_DURATION("audio", "MixStage::ReconcileClocksAndSetStepSize");

  auto& source_clock = stream.reference_clock();
  auto& dest_clock = reference_clock();

  // Right upfront, capture current states for the source and destination clocks.
  auto source_ref_to_clock_mono = source_clock.ref_clock_to_clock_mono();
  auto dest_ref_to_mono = dest_clock.ref_clock_to_clock_mono();

  // UpdateSourceTrans
  //
  // Ensure the mappings from source-frame to source-ref-time and monotonic-time are up-to-date.
  auto snapshot = stream.ref_time_to_frac_presentation_frame();
  info.source_ref_clock_to_frac_source_frames = snapshot.timeline_function;

  if (info.source_ref_clock_to_frac_source_frames.subject_delta() == 0) {
    info.clock_mono_to_frac_source_frames = TimelineFunction();
    info.dest_frames_to_frac_source_frames = TimelineFunction();
    bookkeeping.step_size = Fixed(0);
    bookkeeping.SetRateModuloAndDenominator(0, 1, &info);

    return;
  }

  // Ensure the mappings from source-frame to monotonic-time is up-to-date.
  auto frac_source_frame_to_clock_mono =
      source_ref_to_clock_mono * info.source_ref_clock_to_frac_source_frames.Inverse();
  info.clock_mono_to_frac_source_frames = frac_source_frame_to_clock_mono.Inverse();
  FX_LOGS(TRACE) << clock::TimelineFunctionToString(info.clock_mono_to_frac_source_frames,
                                                    "mono-to-frac-source");

  // Assert we can map from local monotonic-time to fractional source frames.
  FX_DCHECK(info.clock_mono_to_frac_source_frames.rate().reference_delta());

  // UpdateDestTrans
  //
  // Ensure the mappings from dest-frame to monotonic-time is up-to-date.
  // We should only be here if we have a valid mix job. This means a job which supplies a valid
  // transformation from reference time to destination frames (based on dest frame rate).
  FX_DCHECK(cur_mix_job_.dest_ref_clock_to_frac_dest_frame.rate().reference_delta());
  if (cur_mix_job_.dest_ref_clock_to_frac_dest_frame.subject_delta() == 0) {
    info.dest_frames_to_frac_source_frames = TimelineFunction();
    bookkeeping.step_size = Fixed(0);
    bookkeeping.SetRateModuloAndDenominator(0, 1, &info);

    return;
  }

  auto dest_frames_to_dest_ref =
      ReferenceClockToIntegralFrames(cur_mix_job_.dest_ref_clock_to_frac_dest_frame).Inverse();

  // Compose our transformation from local monotonic-time to dest frames.
  auto dest_frames_to_clock_mono = dest_ref_to_mono * dest_frames_to_dest_ref;
  FX_LOGS(TRACE) << clock::TimelineFunctionToString(dest_frames_to_clock_mono, "dest-to-mono");

  // ComposeDestToSource
  //
  // Compose our transformation from destination frames to source fractional frames.
  info.dest_frames_to_frac_source_frames =
      info.clock_mono_to_frac_source_frames * dest_frames_to_clock_mono;
  FX_LOGS(TRACE) << clock::TimelineFunctionToString(info.dest_frames_to_frac_source_frames,
                                                    "dest-to-frac-src (with clocks)");

  // ComputeFrameRateConversionRatio
  //
  // Calculate the TimelineRate for step_size. No clock effects are included; any "micro-SRC" is
  // applied separately as a subsequent correction factor.
  TimelineRate frac_source_frames_per_dest_frame = TimelineRate::Product(
      dest_frames_to_dest_ref.rate(), info.source_ref_clock_to_frac_source_frames.rate());
  FX_LOGS(TRACE) << clock::TimelineRateToString(frac_source_frames_per_dest_frame,
                                                "dest-to-frac-source rate (no clock effects)");

  auto dest_frame = cur_mix_job_.dest_start_frame;
  auto mono_now_from_dest = zx::time{dest_frames_to_clock_mono.Apply(dest_frame)};

  // Set the position relationship of the source and dest clocks, if one has not yet been set.
  if (!info.initial_position_is_set) {
    JamSyncPositions(source_clock, dest_clock, info, bookkeeping, dest_frame, mono_now_from_dest);
    SetStepSize(info, bookkeeping, frac_source_frames_per_dest_frame);
    return;
  }

  // If no synchronization is needed between these clocks (same clock, or device clocks in same
  // domain), then source-to-dest is precisely the relationship between each side's frame rate.
  if (AudioClock::NoSynchronizationRequired(source_clock, dest_clock)) {
    SetStepSize(info, bookkeeping, frac_source_frames_per_dest_frame);
    return;
  }

  if constexpr (kAllowPositionRollback) {
    // If mix_job starts at a dest position before the prev one ended (within tolerance), roll all
    // positions backwards using step_size so this is not considered a dest position discontinuity.
    if (dest_frame < info.next_dest_frame &&
        dest_frame + kDestPosRollbackTolerance >= info.next_dest_frame) {
      info.AdvanceAllPositionsTo(dest_frame, bookkeeping);
    }
  }

  // Check for dest position discontinuity. If so, reset positions and rate adjustments.
  if (dest_frame != info.next_dest_frame) {
    // Set new running positions, based on E2E clock (not just step_size).
    JamSyncPositions(source_clock, dest_clock, info, bookkeeping, dest_frame, mono_now_from_dest);
    SetStepSize(info, bookkeeping, frac_source_frames_per_dest_frame);
    return;
  }
  // TODO(fxbug.dev/63750): pass through a signal if we expect discontinuity (Play, Pause, packet
  // discontinuity bit); use it to log (or report to inspect) only unexpected discontinuities.
  // Add a test to validate that we log discontinuities only when we should.

  // Project dest and source positions (cur_mix_job_.dest_start_frame, info.next_source_frame)
  // into monotonic time (mono_now_from_dest, mono_now_from_source). Record the difference between
  // them (in ns) as source position error.
  auto mono_now_from_source = zx::time{
      info.clock_mono_to_frac_source_frames.ApplyInverse(info.next_source_frame.raw_value())};

  // Convert both positions to monotonic time and get the delta -- this is source position error
  info.source_pos_error = mono_now_from_source - mono_now_from_dest;

  // If source error exceeds our threshold, allow a discontinuity and reset position and rates.
  if (std::abs(info.source_pos_error.get()) > kMaxErrorThresholdDuration.get()) {
    Reporter::Singleton().MixerClockSkewDiscontinuity(info.source_pos_error);

    JamSyncPositions(source_clock, dest_clock, info, bookkeeping, dest_frame, mono_now_from_dest);
    SetStepSize(info, bookkeeping, frac_source_frames_per_dest_frame);
    return;
  }

  // Allow the clocks to self-synchronize to eliminate the position error. A non-zero return value
  // indicates that they cannot, and we should apply a rate-conversion factor in software.
  auto micro_src_ppm = AudioClock::SynchronizeClocks(source_clock, dest_clock, mono_now_from_dest,
                                                     info.source_pos_error);

  // Incorporate the adjustment into frac_source_frames_per_dest_frame (which determines step size).
  if (micro_src_ppm) {
    TimelineRate micro_src_factor{static_cast<uint64_t>(1'000'000 + micro_src_ppm), 1'000'000};

    // Product may exceed uint64/uint64: allow reduction. step_size can be approximate, as clocks
    // (not SRC/step_size) determine a stream absolute position -- SRC just chases the position.
    frac_source_frames_per_dest_frame =
        TimelineRate::Product(frac_source_frames_per_dest_frame, micro_src_factor,
                              false /* don't require exact precision */);
  }

  SetStepSize(info, bookkeeping, frac_source_frames_per_dest_frame);
}

// Establish specific running position values rather than adjusting clock rates, to bring source and
// dest positions together. We do this when setting the initial position relationship, when dest
// running position jumps unexpectedly, and when the error in source position exceeds our threshold.
void MixStage::JamSyncPositions(AudioClock& source_clock, AudioClock& dest_clock,
                                Mixer::SourceInfo& info, Mixer::Bookkeeping& bookkeeping,
                                int64_t dest_frame, zx::time mono_now_from_dest) {
  auto prev_running_dest_frame = info.next_dest_frame;
  auto prev_running_source_frame = info.next_source_frame;
  double prev_source_pos_error = static_cast<double>(info.source_pos_error.get());
  auto initial_position_was_set = info.initial_position_is_set;

  info.ResetPositions(dest_frame, bookkeeping);

  // Reset accumulated rate adjustment feedback, in the relevant clocks.
  AudioClock::ResetRateAdjustments(source_clock, dest_clock, mono_now_from_dest);

  if constexpr (kLogJamSyncs) {
    std::stringstream common_stream, dest_stream, source_stream;
    common_stream << "; MixStage " << static_cast<void*>(this) << ", SourceInfo "
                  << static_cast<void*>(&info) << "; "
                  << AudioClock::SyncInfo(source_clock, dest_clock);
    dest_stream << "dest " << (dest_clock.is_client_clock() ? "Client" : "Device")
                << (dest_clock.is_adjustable() ? "Adjustable" : "Fixed") << "["
                << static_cast<void*>(&dest_clock) << "]: " << ffl::String::DecRational
                << info.next_dest_frame;
    source_stream << "; src " << (source_clock.is_client_clock() ? "Client" : "Device")
                  << (source_clock.is_adjustable() ? "Adjustable" : "Fixed") << "["
                  << static_cast<void*>(&source_clock) << "]: " << ffl::String::DecRational
                  << info.next_source_frame;

    std::stringstream complete_log_msg;
    if (!initial_position_was_set) {
      complete_log_msg << "JamSync(set initial position): " << dest_stream.str()
                       << source_stream.str() << common_stream.str();
      // Log these at lowest level, but reset the count so we always log the next jam-sync
      jam_sync_count_ = -1;
    } else if (prev_running_dest_frame != dest_frame) {
      complete_log_msg << "JamSync(dest discontinuity)  : " << dest_frame - prev_running_dest_frame
                       << " frames; " << dest_stream.str() << " (expect " << prev_running_dest_frame
                       << ")" << source_stream.str() << " (was " << ffl::String::DecRational
                       << prev_running_source_frame << ") at dest " << mono_now_from_dest.get()
                       << common_stream.str();
    } else {
      complete_log_msg << "JamSync(source discontinuity): "
                       << static_cast<float>(prev_source_pos_error / ZX_USEC(1)) << " us (limit "
                       << static_cast<float>(static_cast<float>(kMaxErrorThresholdDuration.get()) /
                                             ZX_USEC(1))
                       << " us) at dest " << mono_now_from_dest.get() << "; " << dest_stream.str()
                       << source_stream.str() << " (expect " << ffl::String::DecRational
                       << prev_running_source_frame << ")" << common_stream.str();
    }
    if (kJamSyncWarningInterval && (jam_sync_count_ % kJamSyncWarningInterval == 0)) {
      FX_LOGS(WARNING) << complete_log_msg.str() << " (1/" << kJamSyncWarningInterval << ")";
    } else if (kJamSyncInfoInterval && (jam_sync_count_ % kJamSyncInfoInterval == 0)) {
      FX_LOGS(INFO) << complete_log_msg.str() << " (1/" << kJamSyncInfoInterval << ")";
    } else if (kJamSyncTraceInterval && (jam_sync_count_ % kJamSyncTraceInterval == 0)) {
      FX_LOGS(TRACE) << complete_log_msg.str() << " (1/" << kJamSyncTraceInterval << ")";
    }
    ++jam_sync_count_;
  }
}

// From a TimelineRate, calculate the [step_size, denominator, rate_modulo] used by Mixer::Mix()
void MixStage::SetStepSize(Mixer::SourceInfo& info, Mixer::Bookkeeping& bookkeeping,
                           TimelineRate& frac_source_frames_per_dest_frame) {
  bookkeeping.step_size = Fixed::FromRaw(frac_source_frames_per_dest_frame.Scale(1));

  // Now that we have a new step_size, generate new rate_modulo and denominator values to
  // account for step_size's limitations.
  auto new_rate_modulo =
      frac_source_frames_per_dest_frame.subject_delta() -
      (frac_source_frames_per_dest_frame.reference_delta() * bookkeeping.step_size.raw_value());
  auto new_denominator = frac_source_frames_per_dest_frame.reference_delta();

  // Reduce this fraction before setting it.
  TimelineRate reduced_rate{new_rate_modulo, new_denominator};
  bookkeeping.SetRateModuloAndDenominator(reduced_rate.subject_delta(),
                                          reduced_rate.reference_delta(), &info);
}

}  // namespace media::audio
