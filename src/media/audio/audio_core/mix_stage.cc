// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mix_stage.h"

#include <lib/fit/defer.h>
#include <lib/trace/event.h>
#include <lib/zx/clock.h>
#include <zircon/status.h>

#include <limits>
#include <memory>

#include "src/media/audio/audio_core/base_renderer.h"
#include "src/media/audio/audio_core/intermediate_buffer.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

TimelineFunction ReferenceClockToIntegralFrames(
    TimelineFunction reference_clock_to_fractional_frames) {
  TimelineRate frames_per_fractional_frame = TimelineRate(1, Fixed(1).raw_value());
  return TimelineFunction::Compose(TimelineFunction(frames_per_fractional_frame),
                                   reference_clock_to_fractional_frames);
}

zx::duration LeadTimeForMixer(const Format& format, const Mixer& mixer) {
  auto delay_frames = mixer.pos_filter_width().Ceiling();
  TimelineRate ticks_per_frame = format.frames_per_ns().Inverse();
  return zx::duration(ticks_per_frame.Scale(delay_frames));
}

}  // namespace

MixStage::MixStage(const Format& output_format, uint32_t block_size,
                   TimelineFunction reference_clock_to_fractional_frame, AudioClock& audio_clock)
    : MixStage(output_format, block_size,
               fbl::MakeRefCounted<VersionedTimelineFunction>(reference_clock_to_fractional_frame),
               audio_clock) {}

MixStage::MixStage(const Format& output_format, uint32_t block_size,
                   fbl::RefPtr<VersionedTimelineFunction> reference_clock_to_fractional_frame,
                   AudioClock& audio_clock)
    : MixStage(std::make_shared<IntermediateBuffer>(
          output_format, block_size, reference_clock_to_fractional_frame, audio_clock)) {}

MixStage::MixStage(std::shared_ptr<WritableStream> output_stream)
    : ReadableStream(output_stream->format()),
      output_stream_(std::move(output_stream)),
      output_ref_clock_(output_stream_->reference_clock()) {}

std::shared_ptr<Mixer> MixStage::AddInput(std::shared_ptr<ReadableStream> stream,
                                          std::optional<float> initial_dest_gain_db,
                                          Mixer::Resampler resampler_hint) {
  TRACE_DURATION("audio", "MixStage::AddInput");
  if (!stream) {
    FX_LOGS(ERROR) << "Null stream, cannot add";
    return nullptr;
  }

  auto mixer = std::shared_ptr<Mixer>(
      Mixer::Select(stream->format().stream_type(), format().stream_type(), resampler_hint)
          .release());
  if (!mixer) {
    mixer = std::make_unique<audio::mixer::NoOp>();
  }

  if (initial_dest_gain_db) {
    mixer->bookkeeping().gain.SetDestGain(*initial_dest_gain_db);
  }

  stream->SetMinLeadTime(GetMinLeadTime() + LeadTimeForMixer(stream->format(), *mixer));

  FX_LOGS(DEBUG) << "AddInput " << (stream->reference_clock().is_adjustable() ? "" : "non-")
                 << "adjustable "
                 << (stream->reference_clock().is_device_clock() ? "device" : "client") << ", self "
                 << (reference_clock().is_adjustable() ? "" : "non-") << "adjustable "
                 << (reference_clock().is_device_clock() ? "device" : "client");
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

  FX_LOGS(DEBUG) << "RemoveInput " << (it->stream->reference_clock().is_adjustable() ? "" : "non-")
                 << "adjustable "
                 << (it->stream->reference_clock().is_device_clock() ? "device" : "client")
                 << ", self " << (reference_clock().is_adjustable() ? "" : "non-") << "adjustable "
                 << (reference_clock().is_device_clock() ? "device" : "client");

  streams_.erase(it);
}

std::optional<ReadableStream::Buffer> MixStage::ReadLock(zx::time dest_ref_time, int64_t frame,
                                                         uint32_t frame_count) {
  TRACE_DURATION("audio", "MixStage::ReadLock", "frame", frame, "length", frame_count);
  memset(&cur_mix_job_, 0, sizeof(cur_mix_job_));

  auto output_buffer = output_stream_->WriteLock(dest_ref_time, frame, frame_count);
  if (!output_buffer) {
    return std::nullopt;
  }
  FX_DCHECK(output_buffer->start().Floor() == frame);

  auto snapshot = output_stream_->ReferenceClockToFixed();

  cur_mix_job_.buf = static_cast<float*>(output_buffer->payload());
  cur_mix_job_.buf_frames = output_buffer->length().Floor();
  cur_mix_job_.start_pts_of = frame;
  cur_mix_job_.dest_ref_clock_to_frac_dest_frame = snapshot.timeline_function;
  cur_mix_job_.applied_gain_db = fuchsia::media::audio::MUTED_GAIN_DB;

  // Fill the output buffer with silence.
  size_t bytes_to_zero = cur_mix_job_.buf_frames * format().bytes_per_frame();
  std::memset(cur_mix_job_.buf, 0, bytes_to_zero);
  ForEachSource(TaskType::Mix, dest_ref_time);

  // Transfer output_buffer ownership to the read lock via this destructor.
  // TODO(fxbug.dev/50669): If this buffer is not fully consumed, we should save this buffer and
  // reuse it for the next call to ReadLock, rather than mixing new data.
  return std::make_optional<ReadableStream::Buffer>(
      output_buffer->start(), output_buffer->length(), output_buffer->payload(), true,
      cur_mix_job_.usages_mixed, cur_mix_job_.applied_gain_db,
      [output_buffer = std::move(output_buffer)](bool) mutable { output_buffer = std::nullopt; });
}

BaseStream::TimelineFunctionSnapshot MixStage::ReferenceClockToFixed() const {
  TRACE_DURATION("audio", "MixStage::ReferenceClockToFixed");
  return output_stream_->ReferenceClockToFixed();
}

void MixStage::SetMinLeadTime(zx::duration min_lead_time) {
  TRACE_DURATION("audio", "MixStage::SetMinLeadTime");
  ReadableStream::SetMinLeadTime(min_lead_time);

  // Propagate our lead time to our sources.
  std::lock_guard<std::mutex> lock(stream_lock_);
  for (const auto& holder : streams_) {
    FX_DCHECK(holder.stream);
    FX_DCHECK(holder.mixer);

    zx::duration mixer_lead_time = LeadTimeForMixer(holder.stream->format(), *holder.mixer);
    holder.stream->SetMinLeadTime(min_lead_time + mixer_lead_time);
  }
}

void MixStage::Trim(zx::time dest_ref_time) {
  TRACE_DURATION("audio", "MixStage::Trim");
  ForEachSource(TaskType::Trim, dest_ref_time);
}

void MixStage::ForEachSource(TaskType task_type, zx::time dest_ref_time) {
  TRACE_DURATION("audio", "MixStage::ForEachSource");

  std::vector<StreamHolder> sources;
  {
    std::lock_guard<std::mutex> lock(stream_lock_);
    for (const auto& holder : streams_) {
      sources.emplace_back(StreamHolder{holder.stream, holder.mixer});
    }
  }

  for (auto& source : sources) {
    auto mono_time = reference_clock().MonotonicTimeFromReferenceTime(dest_ref_time);
    auto source_ref_time =
        source.stream->reference_clock().ReferenceTimeFromMonotonicTime(mono_time);

    if (task_type == TaskType::Mix) {
      auto& source_info = source.mixer->source_info();
      auto& bookkeeping = source.mixer->bookkeeping();
      ReconcileClocksAndSetStepSize(source_info, bookkeeping, *source.stream);
      MixStream(*source.mixer, *source.stream, source_ref_time);
    } else {
      source.stream->Trim(source_ref_time);
    }
  }
}

void MixStage::MixStream(Mixer& mixer, ReadableStream& stream, zx::time source_ref_time) {
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
  auto frac_source_for_first_mix_job_frame =
      Fixed::FromRaw(info.dest_frames_to_frac_source_frames(cur_mix_job_.start_pts_of));

  while (true) {
    // At this point we know we need to consume some source data, but we don't yet know how much.
    // Here is how many destination frames we still need to produce, for this mix job.
    FX_DCHECK(cur_mix_job_.buf_frames >= info.frames_produced);
    uint32_t dest_frames_left = cur_mix_job_.buf_frames - info.frames_produced;
    if (dest_frames_left == 0) {
      break;
    }

    // Calculate this job's last sampling point.
    Fixed source_frames =
        Fixed::FromRaw(info.dest_frames_to_frac_source_frames.rate().Scale(dest_frames_left)) +
        mixer.pos_filter_width();

    // Try to grab the front of the packet queue (or ring buffer, if capturing).
    auto stream_buffer = stream.ReadLock(
        source_ref_time, frac_source_for_first_mix_job_frame.Floor(), source_frames.Ceiling());

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

    frac_source_for_first_mix_job_frame = stream_buffer->end();
  }

  // If there was insufficient supply to meet our demand, we may not have mixed enough frames, but
  // we advance our destination frame count as if we did, because time rolls on. Same for source.
  auto& bookkeeping = mixer.bookkeeping();
  info.AdvanceRunningPositionsTo(cur_mix_job_.start_pts_of + cur_mix_job_.buf_frames, bookkeeping);
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
  uint32_t dest_frames_left = cur_mix_job_.buf_frames - info.frames_produced;
  float* buf = cur_mix_job_.buf + (info.frames_produced * format().channels());

  // Determine this job's first and last sampling points, in source sub-frames. Use the next
  // expected source position (in frac_frames) saved in our long-running position accounting.
  Fixed frac_source_for_first_mix_job_frame = info.next_frac_source_frame;

  // This represents the last possible source frame we need for this mix. Note that it is 1 subframe
  // short of the source needed for the SUBSEQUENT dest frame, floored to an integral source frame.
  // We cannot just subtract one integral frame from the source corresponding to the next start dest
  // because very large or small step_size values make this 1-frame assumption invalid.
  //
  auto frac_source_for_final_mix_job_frame =
      Fixed::FromRaw(frac_source_for_first_mix_job_frame.raw_value() +
                     (bookkeeping.step_size * dest_frames_left +
                      (bookkeeping.rate_modulo * dest_frames_left + bookkeeping.src_pos_modulo) /
                          bookkeeping.denominator) -
                     1);

  // The above two calculated values characterize our demand. Now reason about our supply.
  //
  // Assert our implementation-defined limit is compatible with the FIDL limit. The latter is
  // already enforced by the renderer implementation.
  static_assert(fuchsia::media::MAX_FRAMES_PER_RENDERER_PACKET <= Fixed::Max().Floor());
  FX_DCHECK(source_buffer.end() > source_buffer.start());
  FX_DCHECK(source_buffer.length() <= Fixed(Fixed::Max()));

  // Calculate the actual first and final frame times in the source packet.
  Fixed frac_source_for_first_packet_frame = source_buffer.start();
  Fixed frac_source_for_final_packet_frame = source_buffer.end() - Fixed(1);

  // If this source packet's final audio frame occurs before our filter's negative edge, centered at
  // our first sampling point, then this packet is entirely in the past and may be skipped.
  // Returning true means we're done with the packet (it can be completed) and we would like another
  if (frac_source_for_final_packet_frame <
      (frac_source_for_first_mix_job_frame - mixer.neg_filter_width())) {
    Fixed source_frac_frames_late = frac_source_for_first_mix_job_frame - mixer.neg_filter_width() -
                                    frac_source_for_first_packet_frame;
    auto clock_mono_late = zx::nsec(info.clock_mono_to_frac_source_frames.rate().Inverse().Scale(
        source_frac_frames_late.raw_value()));

    stream.ReportUnderflow(frac_source_for_first_packet_frame, frac_source_for_first_mix_job_frame,
                           clock_mono_late);
    return true;
  }

  // If this source packet's first audio frame occurs after our filter's positive edge, centered at
  // our final sampling point, then this packet is entirely in the future and should be held.
  // Returning false (based on requirement that packets must be presented in timestamp-chronological
  // order) means that we have consumed all of the available packet "supply" as we can at this time.
  if (frac_source_for_first_packet_frame >
      (frac_source_for_final_mix_job_frame + mixer.pos_filter_width())) {
    return false;
  }

  // If neither of the above, then evidently this source packet intersects our mixer's filter.
  // Compute the offset into the dest buffer where our first generated sample should land, and the
  // offset into the source packet where we should start sampling.
  int64_t dest_offset_64 = 0;
  Fixed frac_source_offset_64 =
      frac_source_for_first_mix_job_frame - frac_source_for_first_packet_frame;
  Fixed frac_source_pos_edge_first_mix_frame =
      frac_source_for_first_mix_job_frame + mixer.pos_filter_width();

  // If the packet's first frame comes after the filter window's positive edge,
  // then we should skip some frames in the destination buffer before starting to produce data.
  if (frac_source_for_first_packet_frame > frac_source_pos_edge_first_mix_frame) {
    const TimelineRate& dest_to_src = info.dest_frames_to_frac_source_frames.rate();
    // The dest_buffer offset is based on the distance from mix job start to packet start (measured
    // in frac_frames), converted into frames in the destination timeline. As we scale the
    // frac_frame delta into dest frames, we want to "round up" any subframes that are present; any
    // src subframes should push our dest frame up to the next integer. To do this, we subtract a
    // single subframe (guaranteeing that the zero-fraction src case will truncate down), then scale
    // the src delta to dest frames (which effectively truncates any resultant fraction in the
    // computed dest frame), then add an additional 'round-up' frame (to account for initial
    // subtract). Because we entered this IF in the first place, we have at least some fractional
    // src delta, thus dest_offset_64 is guaranteed to become greater than zero.
    Fixed first_source_mix_point =
        frac_source_for_first_packet_frame - frac_source_pos_edge_first_mix_frame;
    dest_offset_64 = dest_to_src.Inverse().Scale(first_source_mix_point.raw_value() - 1) + 1;
    FX_DCHECK(dest_offset_64 > 0);

    frac_source_offset_64 += Fixed::FromRaw(dest_to_src.Scale(dest_offset_64));

    // Packet is within the mix window but starts after mix start. MixStream breaks mix jobs into
    // multiple pieces so that each packet gets its own ProcessMix call; this means there was no
    // contiguous packet immediately before this one. For now we don't report this as a problem;
    // eventually (when we can rely on clients to accurately set STREAM_PACKET_FLAG_DISCONTINUITY),
    // we should report this as a minor discontinuity if that flag is NOT set -- via something like
    //    stream.ReportPartialUnderflow(frac_source_offset_64,dest_offset_64)
    //
    // TODO(mpuryear): move packet discontinuity (gap/overlap) detection up into the
    // Renderer/PacketQueue, and remove PartialUnderflow reporting and the metric altogether.
  }

  FX_DCHECK(dest_offset_64 >= 0);
  FX_DCHECK(dest_offset_64 <= static_cast<int64_t>(dest_frames_left));
  auto dest_offset = static_cast<uint32_t>(dest_offset_64);

  FX_DCHECK(frac_source_offset_64 <= std::numeric_limits<int32_t>::max());
  FX_DCHECK(frac_source_offset_64 >= std::numeric_limits<int32_t>::min());
  auto frac_source_offset = Fixed(frac_source_offset_64);

  // Looks like we are ready to go. Mix.
  FX_DCHECK(frac_source_offset + mixer.pos_filter_width() >= Fixed(0));
  bool consumed_source;
  if (dest_offset >= dest_frames_left) {
    // We initially needed to source frames from this packet in order to finish this mix. After
    // realigning our sampling point to the nearest dest frame, that dest frame is now at or beyond
    // the end of this mix job. We have no need to mix any source material now, just exit.
    consumed_source = false;
  } else if (frac_source_offset + mixer.pos_filter_width() >= source_buffer.length()) {
    // This packet was initially within our mix window. After realigning our sampling point to the
    // nearest dest frame, it is now entirely in the past. This can only occur when down-sampling
    // and is made more likely if the rate conversion ratio is very high. We've already reported
    // a partial underflow when realigning, so just complete the packet and move on to the next.
    consumed_source = true;
  } else {
    // When calling Mix(), we communicate the resampling rate with three parameters. We augment
    // step_size with rate_modulo and denominator arguments that capture the remaining rate
    // component that cannot be expressed by a 19.13 fixed-point step_size. Note: step_size and
    // frac_source_offset use the same format -- they have the same limitations in what they can and
    // cannot communicate.
    //
    // For perfect position accuracy, just as we track incoming/outgoing fractional source offset,
    // we also need to track the ongoing subframe_position_modulo. This is now added to Mix() and
    // maintained across calls, but not initially set to any value other than zero. For now, we are
    // deferring that work, since any error would be less than 1 fractional frame.
    //
    // Q: Why did we solve this issue for Rate but not for initial Position?
    // A: We solved this issue for *rate* because its effect accumulates over time, causing clearly
    // measurable distortion that becomes crippling with larger jobs. For *position*, there is no
    // accumulated magnification over time -- in analyzing the distortion that this should cause,
    // mix job size affects the distortion's frequency but not its amplitude. We expect the effects
    // to be below audible thresholds. Until the effects are measurable and attributable to this
    // jitter, we will defer this work.
    auto prev_dest_offset = dest_offset;
    auto dest_ref_clock_to_integral_dest_frame =
        ReferenceClockToIntegralFrames(cur_mix_job_.dest_ref_clock_to_frac_dest_frame);

    // Check whether we are still ramping
    bool ramping = bookkeeping.gain.IsRamping();
    if (ramping) {
      bookkeeping.gain.GetScaleArray(
          bookkeeping.scale_arr.get(),
          std::min(dest_frames_left - dest_offset, Mixer::Bookkeeping::kScaleArrLen),
          dest_ref_clock_to_integral_dest_frame.rate());
    }

    {
      int32_t raw_source_offset = frac_source_offset.raw_value();
      consumed_source = mixer.Mix(buf, dest_frames_left, &dest_offset, source_buffer.payload(),
                                  source_buffer.length().raw_value(), &raw_source_offset,
                                  cur_mix_job_.accumulate);
      frac_source_offset = Fixed::FromRaw(raw_source_offset);
      cur_mix_job_.usages_mixed.insert_all(source_buffer.usage_mask());
      // The gain for the stream will be any previously applied gain combined with any additional
      // gain that will be applied at this stage. In terms of the applied gain of the mixed stream,
      // we consider that to be the max gain of any single source stream.
      float stream_gain_db =
          Gain::CombineGains(source_buffer.gain_db(), bookkeeping.gain.GetGainDb());
      cur_mix_job_.applied_gain_db = std::max(cur_mix_job_.applied_gain_db, stream_gain_db);
    }

    // If src is ramping, advance that ramp by the amount of dest that was just mixed.
    if (ramping) {
      bookkeeping.gain.Advance(dest_offset - prev_dest_offset,
                               dest_ref_clock_to_integral_dest_frame.rate());
    }
  }

  FX_DCHECK(dest_offset <= dest_frames_left);
  info.AdvanceRunningPositionsBy(dest_offset, bookkeeping);

  if (consumed_source) {
    FX_DCHECK(frac_source_offset + mixer.pos_filter_width() >= source_buffer.length());
  }

  info.frames_produced += dest_offset;
  FX_DCHECK(info.frames_produced <= cur_mix_job_.buf_frames);

  return consumed_source;
}

// We compose the effects of clock reconciliation into our sample-rate-conversion step size, but
// only for streams that use neither the 'optimal' clock, nor the clock we designate as driving our
// hardware-rate-adjustments. We apply this micro-SRC via an intermediate "slew away the error"
// rate-correction factor driven by a PID control. Why use a PID? Sources do not merely chase the
// other clock's rate -- they chase its position. Note that even if we don't adjust our rate, we
// still want a composed transformation for offsets.
//
// Calculate the composed dest-to-src transformation and update the mixer's bookkeeping for
// step_size etc. These are the only deliverables for this method.
void MixStage::ReconcileClocksAndSetStepSize(Mixer::SourceInfo& info,
                                             Mixer::Bookkeeping& bookkeeping,
                                             ReadableStream& stream) {
  constexpr zx::duration kMaxErrorThresholdDuration = zx::msec(2);

  TRACE_DURATION("audio", "MixStage::ReconcileClocksAndSetStepSize");

  // UpdateSourceTrans
  //
  // Ensure the mappings from source-frame to source-ref-time and monotonic-time are up-to-date.
  auto snapshot = stream.ReferenceClockToFixed();
  info.source_ref_clock_to_frac_source_frames = snapshot.timeline_function;

  if (info.source_ref_clock_to_frac_source_frames.subject_delta() == 0) {
    info.clock_mono_to_frac_source_frames = TimelineFunction();
    info.dest_frames_to_frac_source_frames = TimelineFunction();
    bookkeeping.step_size = 0;
    bookkeeping.denominator = 0;  // we need not also clear rate_mod and pos_mod

    return;
  }

  FX_DCHECK(stream.reference_clock().is_valid());
  FX_DCHECK(reference_clock().is_valid());

  // Ensure the mappings from source-frame to monotonic-time is up-to-date.
  auto source_ref_clock_to_clock_mono = stream.reference_clock().ref_clock_to_clock_mono();
  auto frac_source_frame_to_clock_mono =
      source_ref_clock_to_clock_mono * info.source_ref_clock_to_frac_source_frames.Inverse();
  info.clock_mono_to_frac_source_frames = frac_source_frame_to_clock_mono.Inverse();
  // Assert we can map from local monotonic-time to fractional source frames.
  FX_DCHECK(info.clock_mono_to_frac_source_frames.rate().reference_delta());

  // UpdateDestTrans
  //
  // Ensure the mappings from dest-frame to monotonic-time is up-to-date.
  // We should only be here if we have a valid mix job. This means a job which supplies a valid
  // transformation from reference time to destination frames (based on dest frame rate).
  FX_DCHECK(cur_mix_job_.dest_ref_clock_to_frac_dest_frame.rate().reference_delta());
  if (cur_mix_job_.dest_ref_clock_to_frac_dest_frame.rate().subject_delta() == 0) {
    info.dest_frames_to_frac_source_frames = TimelineFunction();
    bookkeeping.step_size = 0;
    bookkeeping.denominator = 0;  // we need not also clear rate_mod and pos_mod

    return;
  }

  auto dest_frames_to_dest_ref_clock =
      ReferenceClockToIntegralFrames(cur_mix_job_.dest_ref_clock_to_frac_dest_frame).Inverse();

  // Compose our transformation from local monotonic-time to dest frames.
  auto dest_frames_to_clock_mono =
      reference_clock().ref_clock_to_clock_mono() * dest_frames_to_dest_ref_clock;

  // ComposeDestToSource
  //
  // Compose our transformation from destination frames to source fractional frames.
  //
  // Combine the job-supplied dest transformation, with the renderer-supplied mapping of
  // monotonic-to-source-subframe, to produce a transformation which maps from dest frames to
  // fractional source frames.
  info.dest_frames_to_frac_source_frames =
      info.clock_mono_to_frac_source_frames * dest_frames_to_clock_mono;

  // ComputeFrameRateConversionRatio
  //
  // Determine the appropriate TimelineRate for step_size. This based exclusively on the frame rates
  // of the source and destination. If we happen to be applying "micro-SRC" for this source, then
  // that will be included subsequently as a correction factor.
  TimelineRate frac_src_frames_per_dest_frame =
      dest_frames_to_dest_ref_clock.rate() * info.source_ref_clock_to_frac_source_frames.rate();

  // SynchronizeClocks
  //
  // If client clock and device clock differ, reconcile them.
  // For start dest frame, measure [predicted - actual] error (in frac_src) since last mix. Do this
  // even if clocks are same on both sides, as this allows us to perform an initial sync-up between
  // running position accounting and the initial clock transforms (even those with offsets).
  auto curr_dest_frame = cur_mix_job_.start_pts_of;
  if (info.next_dest_frame != curr_dest_frame || !info.running_pos_established) {
    info.running_pos_established = true;

    // Set new running positions, based on the E2E clock (not just from step_size)
    auto prev_running_dest_frame = info.next_dest_frame;
    auto prev_running_frac_src_frame = info.next_frac_source_frame;
    info.ResetPositions(curr_dest_frame, bookkeeping);

    FX_LOGS(DEBUG) << "Running dest position is discontinuous (expected " << prev_running_dest_frame
                   << ", actual " << curr_dest_frame << ") updating running source position from "
                   << prev_running_frac_src_frame.raw_value() << " to "
                   << info.next_frac_source_frame.raw_value();

    // Also should reset the PID controls in the relevant clocks.
    reference_clock().ResetRateAdjustmentTuning(curr_dest_frame);
    stream.reference_clock().ResetRateAdjustmentTuning(curr_dest_frame);
  } else if (reference_clock() == stream.reference_clock()) {
    // Same clock on both sides can occur when multiple MixStages are connected serially (should
    // both be device clocks). Don't synchronize: use frac_src_frames_per_dest_frame as-is.
  } else if (reference_clock().is_device_clock() && stream.reference_clock().is_device_clock()) {
    // We are synchronizing two device clocks. Unfortunately, we know they aren't the same.
    // To enable this scenario in the future, at least one must be adjustable in hardware (it will
    // be the "follower"); the other must be marked as hardware_controlling.
    FX_DCHECK(reference_clock() == stream.reference_clock())
        << "Cannot reconcile two different device clocks: clock routing error";
  } else {
    // MeasureClockError
    //
    // Between upstream (source) and downstream (dest) clocks, we should have one device clock
    // and one client clock. Remember capture mix, where usual roles are reversed.
    // They can't BOTH be client clocks (although we could handle this, if routing existed).
    FX_DCHECK(reference_clock().is_device_clock() || stream.reference_clock().is_device_clock())
        << "Cannot reconcile two client clocks. No device clock: clock routing error";

    // MeasureClockError
    //
    // Measure the error in src_frac_pos
    auto max_error_frac =
        info.source_ref_clock_to_frac_source_frames.rate().Scale(kMaxErrorThresholdDuration.get());

    auto curr_src_frac_pos =
        Fixed::FromRaw(info.dest_frames_to_frac_source_frames(curr_dest_frame));
    info.frac_source_error = info.next_frac_source_frame - curr_src_frac_pos;

    // AdjustClock
    //
    //   If mix is continuous since then, report the latest error to the AudioClock
    //   AudioClock feeds errors to its internal PID, producing a running correction factor....
    //   ... If clock is hardware-controlling, apply correction factor to the hardware.
    //   ... If clock is optimal, apply correction factor directly to the client clock.
    //   ... Otherwise, expose this error correction factor directly to MixStage
    AudioClock& device_clock =
        stream.reference_clock().is_device_clock() ? stream.reference_clock() : reference_clock();
    AudioClock& client_clock =
        stream.reference_clock().is_device_clock() ? reference_clock() : stream.reference_clock();

    if (std::abs(info.frac_source_error.raw_value()) > max_error_frac) {
      // Source error exceeds our threshold
      // Reset the rate adjustment process altogether and allow a discontinuity
      info.next_frac_source_frame = curr_src_frac_pos;
      FX_LOGS(DEBUG) << "frac_source_error: out of bounds (" << info.frac_source_error.raw_value()
                     << " vs. limit +/-" << max_error_frac << "), resetting next_frac_src to "
                     << info.next_frac_source_frame.raw_value();

      // Reset the PID controls, in the relevant clocks.
      client_clock.ResetRateAdjustmentTuning(curr_dest_frame);
      device_clock.ResetRateAdjustmentTuning(curr_dest_frame);
    } else {
      // No error is too small to worry about; handle them all.
      FX_LOGS(TRACE) << "frac_source_error: tuning reference clock at dest " << curr_dest_frame
                     << " for " << info.frac_source_error.raw_value();
      if (client_clock.is_adjustable()) {
        // Adjust client_clock, the 'flexible' clock that we have provided to the client
      } else if (device_clock.is_adjustable() && client_clock.controls_hardware_clock()) {
        // Adjust device_clock's hardware clock rate based on the frac_source_error
      } else {
        client_clock.TuneRateForError(info.frac_source_error, curr_dest_frame);

        // Using this rate adjustment factor, adjust step_size, so future src_positions will
        // converge to what these two clocks require.
        // Multiplying these factors can exceed TimelineRate's uint32/uint32 resolution so we allow
        // reduced precision, if required.
        TimelineRate micro_src_factor = client_clock.rate_adjustment();
        frac_src_frames_per_dest_frame =
            TimelineRate::Product(frac_src_frames_per_dest_frame, micro_src_factor, false);

        while (frac_src_frames_per_dest_frame.subject_delta() >
                   std::numeric_limits<uint32_t>::max() ||
               frac_src_frames_per_dest_frame.reference_delta() >
                   std::numeric_limits<uint32_t>::max()) {
          frac_src_frames_per_dest_frame =
              TimelineRate((frac_src_frames_per_dest_frame.subject_delta() + 1) >> 1,
                           frac_src_frames_per_dest_frame.reference_delta() >> 1);
          // clock::DisplayTimelineRate(frac_src_frames_per_dest_frame,"Reduced frac-to-dest:");
        }
      }
    }
  }

  // SetStepSize
  //
  // Convert the TimelineRate into step_size, denominator and rate_modulo -- as usual.
  // Finally, compute the step size in subframes. IOW, every time we move forward one dest
  // frame, how many source subframes should we consume.
  FX_DCHECK(frac_src_frames_per_dest_frame.reference_delta());
  int64_t tmp_step_size = frac_src_frames_per_dest_frame.Scale(1);

  FX_DCHECK(tmp_step_size >= 0);
  FX_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());

  auto old_denominator = bookkeeping.denominator;
  bookkeeping.step_size = static_cast<uint32_t>(tmp_step_size);
  bookkeeping.denominator = frac_src_frames_per_dest_frame.reference_delta();
  bookkeeping.rate_modulo = frac_src_frames_per_dest_frame.subject_delta() -
                            (bookkeeping.denominator * bookkeeping.step_size);

  // Update the source position modulos, if the denominator is changing.
  if (old_denominator != bookkeeping.denominator) {
    if (old_denominator) {
      bookkeeping.src_pos_modulo *= bookkeeping.denominator;
      info.next_src_pos_modulo *= bookkeeping.denominator;
      bookkeeping.src_pos_modulo =
          std::round(static_cast<float>(bookkeeping.src_pos_modulo) / old_denominator);
      info.next_src_pos_modulo =
          std::round(static_cast<float>(info.next_src_pos_modulo) / old_denominator);
    } else {
      bookkeeping.src_pos_modulo = info.next_src_pos_modulo = 0;
    }
  }
  // Else preserve the previous source position modulo values from before
}

}  // namespace media::audio
