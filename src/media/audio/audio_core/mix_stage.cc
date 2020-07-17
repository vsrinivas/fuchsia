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
#include "src/media/audio/lib/logging/logging.h"

namespace media::audio {
namespace {

TimelineFunction ReferenceClockToIntegralFrames(
    TimelineFunction reference_clock_to_fractional_frames) {
  TimelineRate frames_per_fractional_frame =
      TimelineRate(1, FractionalFrames<uint32_t>(1).raw_value());
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

  stream->SetMinLeadTime(GetMinLeadTime() + LeadTimeForMixer(stream->format(), *mixer));
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

  auto snapshot = output_stream_->ReferenceClockToFractionalFrames();

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
  // TODO(50669): If this buffer is not fully consumed, we should save this buffer and reuse it for
  // the next call to ReadLock, rather than mixing new data.
  return std::make_optional<ReadableStream::Buffer>(
      output_buffer->start(), output_buffer->length(), output_buffer->payload(), true,
      cur_mix_job_.usages_mixed, cur_mix_job_.applied_gain_db,
      [output_buffer = std::move(output_buffer)](bool) mutable { output_buffer = std::nullopt; });
}

BaseStream::TimelineFunctionSnapshot MixStage::ReferenceClockToFractionalFrames() const {
  TRACE_DURATION("audio", "MixStage::ReferenceClockToFractionalFrames");
  return output_stream_->ReferenceClockToFractionalFrames();
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

  std::vector<MixerInput> streams;
  {
    std::lock_guard<std::mutex> lock(stream_lock_);
    for (const auto& holder : streams_) {
      if (holder.stream && holder.mixer) {
        auto mono_result = reference_clock().MonotonicTimeFromReferenceTime(dest_ref_time);
        if (mono_result.is_error()) {
          FX_LOGS_FIRST_N(ERROR, 200) << "Error converting dest ref time to monotonic time";
          // Our destination clock isn't working. We won't be able to mix any of these streams.
          break;
        }
        auto mono_time = mono_result.take_value();

        auto source_ref_result =
            holder.stream->reference_clock().ReferenceTimeFromMonotonicTime(mono_time);
        if (source_ref_result.is_error()) {
          FX_LOGS_FIRST_N(ERROR, 200) << "Error converting monotonic time to source ref time";
          // Our source clock isn't working. We won't be able to mix this one stream.
          continue;
        }
        auto source_ref_time = source_ref_result.take_value();

        // Ensure the mappings from source-frame to source-ref-time and monotonic-time are
        // up-to-date.
        //
        // TODO(55851) We need to do this here because when source is a renderers PacketQueue, the
        // clock in the AudioRenderer can be free'd while we're in the process of mixing, so it's
        // not safe to reference source->reference_clock outside of this lock (which
        // UpdateSourceTrans does).
        if (task_type == TaskType::Mix) {
          ReconcileClocksAndSetStepSize(holder);
        }

        streams.emplace_back(MixerInput(holder.stream, holder.mixer, source_ref_time));
      }
    }
  }

  for (const auto& input : streams) {
    if (task_type == TaskType::Mix) {
      MixStream(input);
    } else {
      input.Trim();
    }
  }
}

void MixStage::MixStream(const MixStage::MixerInput& input) {
  TRACE_DURATION("audio", "MixStage::MixStream");
  cur_mix_job_.frames_produced = 0;
  Mixer* mixer = input.mixer();
  zx::time source_ref_time = input.ref_time();

  // If the renderer is currently paused, subject_delta (not just step_size) is zero. This packet
  // may be relevant eventually, but currently it contributes nothing.
  if (!mixer->bookkeeping().dest_frames_to_frac_source_frames.subject_delta()) {
    return;
  }

  // Calculate the first sampling point for the initial job, in source sub-frames. Use timestamps
  // for the first and last dest frames we need, translated into the source (frac_frame) timeline.
  auto& info = mixer->bookkeeping();
  auto frac_source_for_first_mix_job_frame =
      FractionalFrames<int64_t>::FromRaw(info.dest_frames_to_frac_source_frames(
          cur_mix_job_.start_pts_of + cur_mix_job_.frames_produced));

  while (true) {
    // At this point we know we need to consume some source data, but we don't yet know how much.
    // Here is how many destination frames we still need to produce, for this mix job.
    uint32_t dest_frames_left = cur_mix_job_.buf_frames - cur_mix_job_.frames_produced;
    if (dest_frames_left == 0) {
      break;
    }

    // Calculate this job's last sampling point.
    FractionalFrames<int64_t> source_frames =
        FractionalFrames<int64_t>::FromRaw(
            info.dest_frames_to_frac_source_frames.rate().Scale(dest_frames_left)) +
        mixer->pos_filter_width();

    // Try to grab the front of the packet queue (or ring buffer, if capturing).
    auto stream_buffer = input.ReadLock(
        source_ref_time, frac_source_for_first_mix_job_frame.Floor(), source_frames.Ceiling());

    // If the queue is empty, then we are done.
    if (!stream_buffer) {
      break;
    }

    // If the packet is discontinuous, reset our mixer's internal filter state.
    if (!stream_buffer->is_continuous()) {
      mixer->Reset();
    }

    // Now process the packet at the front of the renderer's queue. If the packet has been
    // entirely consumed, pop it off the front and proceed to the next. Otherwise, we are done.
    auto fully_consumed = ProcessMix(input, *stream_buffer);
    stream_buffer->set_is_fully_consumed(fully_consumed);

    // If we have mixed enough destination frames, we are done with this mix, regardless of what
    // we should now do with the source packet.
    if (cur_mix_job_.frames_produced == cur_mix_job_.buf_frames) {
      break;
    }
    // If we still need to produce more destination data, but could not complete this source
    // packet (we're paused, or the packet is in the future), then we are done.
    if (!fully_consumed) {
      break;
    }

    frac_source_for_first_mix_job_frame = stream_buffer->end();
  }

  cur_mix_job_.accumulate = true;
}

bool MixStage::ProcessMix(const MixStage::MixerInput& input,
                          const ReadableStream::Buffer& source_buffer) {
  TRACE_DURATION("audio", "MixStage::ProcessMix");
  Mixer* mixer = input.mixer();
  // Bookkeeping should contain: the rechannel matrix (eventually).

  // Sanity check our parameters.
  FX_DCHECK(mixer);

  // We had better have a valid job, or why are we here?
  if (cur_mix_job_.buf_frames == 0) {
    return false;
  }

  // Have we produced enough? If so, hold this packet and move to next renderer.
  if (cur_mix_job_.frames_produced >= cur_mix_job_.buf_frames) {
    return false;
  }

  auto& info = mixer->bookkeeping();

  // If the renderer is currently paused, subject_delta (not just step_size) is zero. This packet
  // may be relevant eventually, but currently it contributes nothing. Tell ForEachSource we are
  // done, but hold the packet for now.
  if (!info.dest_frames_to_frac_source_frames.subject_delta()) {
    return false;
  }

  // At this point we know we need to consume some source data, but we don't yet know how much.
  // Here is how many destination frames we still need to produce, for this mix job.
  uint32_t dest_frames_left = cur_mix_job_.buf_frames - cur_mix_job_.frames_produced;
  float* buf = cur_mix_job_.buf + (cur_mix_job_.frames_produced * format().channels());

  // Calculate this job's first and last sampling points, in source sub-frames. Use timestamps for
  // the first and last dest frames we need, translated into the source (frac_frame) timeline.
  FractionalFrames<int64_t> frac_source_for_first_mix_job_frame =
      FractionalFrames<int64_t>::FromRaw(info.dest_frames_to_frac_source_frames(
          cur_mix_job_.start_pts_of + cur_mix_job_.frames_produced));

  // This represents (in the frac_frame source timeline) the time of the LAST dest frame we need.
  // Without the "-1", this would be the first destination frame of the NEXT job.
  FractionalFrames<int64_t> frac_source_for_final_mix_job_frame =
      frac_source_for_first_mix_job_frame +
      FractionalFrames<int64_t>::FromRaw(
          info.dest_frames_to_frac_source_frames.rate().Scale(dest_frames_left - 1));

  // The above two calculated values characterize our demand. Now reason about our supply.
  //
  // If packet has no frames, there's no need to mix it; it may be skipped.
  if (source_buffer.end() == source_buffer.start()) {
    AUDIO_LOG(DEBUG) << " skipping an empty packet!";
    return true;
  }

  FX_DCHECK(source_buffer.end() >= source_buffer.start() + 1);

  // Calculate the actual first and final frame times in the source packet.
  FractionalFrames<int64_t> frac_source_for_first_packet_frame = source_buffer.start();
  FractionalFrames<int64_t> frac_source_for_final_packet_frame = source_buffer.end() - 1;

  // If this source packet's final audio frame occurs before our filter's negative edge, centered at
  // our first sampling point, then this packet is entirely in the past and may be skipped.
  // Returning true means we're done with the packet (it can be completed) and we would like another
  if (frac_source_for_final_packet_frame <
      (frac_source_for_first_mix_job_frame - mixer->neg_filter_width())) {
    FractionalFrames<int64_t> source_frac_frames_late = frac_source_for_first_mix_job_frame -
                                                        mixer->neg_filter_width() -
                                                        frac_source_for_first_packet_frame;
    auto clock_mono_late = zx::nsec(info.clock_mono_to_frac_source_frames.rate().Inverse().Scale(
        source_frac_frames_late.raw_value()));

    input.ReportUnderflow(frac_source_for_first_packet_frame, frac_source_for_first_mix_job_frame,
                          clock_mono_late);

    return true;
  }

  // If this source packet's first audio frame occurs after our filter's positive edge, centered at
  // our final sampling point, then this packet is entirely in the future and should be held.
  // Returning false (based on requirement that packets must be presented in timestamp-chronological
  // order) means that we have consumed all of the available packet "supply" as we can at this time.
  if (frac_source_for_first_packet_frame >
      (frac_source_for_final_mix_job_frame + mixer->pos_filter_width())) {
    return false;
  }

  // If neither of the above, then evidently this source packet intersects our mixer's filter.
  // Compute the offset into the dest buffer where our first generated sample should land, and the
  // offset into the source packet where we should start sampling.
  int64_t dest_offset_64 = 0;
  FractionalFrames<int64_t> frac_source_offset_64 =
      frac_source_for_first_mix_job_frame - frac_source_for_first_packet_frame;
  FractionalFrames<int64_t> frac_source_pos_edge_first_mix_frame =
      frac_source_for_first_mix_job_frame + mixer->pos_filter_width();

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
    FractionalFrames<int64_t> first_source_mix_point =
        frac_source_for_first_packet_frame - frac_source_pos_edge_first_mix_frame;
    dest_offset_64 = dest_to_src.Inverse().Scale(first_source_mix_point.raw_value() - 1) + 1;
    FX_DCHECK(dest_offset_64 > 0);

    frac_source_offset_64 += FractionalFrames<int64_t>::FromRaw(dest_to_src.Scale(dest_offset_64));

    input.ReportPartialUnderflow(frac_source_offset_64, dest_offset_64);
  }

  FX_DCHECK(dest_offset_64 >= 0);
  FX_DCHECK(dest_offset_64 < static_cast<int64_t>(dest_frames_left));
  auto dest_offset = static_cast<uint32_t>(dest_offset_64);

  FX_DCHECK(frac_source_offset_64 <= std::numeric_limits<int32_t>::max());
  FX_DCHECK(frac_source_offset_64 >= std::numeric_limits<int32_t>::min());
  auto frac_source_offset = FractionalFrames<int32_t>(frac_source_offset_64);

  // Looks like we are ready to go. Mix.
  FX_DCHECK(source_buffer.length() <= FractionalFrames<uint32_t>(FractionalFrames<int32_t>::Max()));

  FX_DCHECK(frac_source_offset + mixer->pos_filter_width() >= FractionalFrames<uint32_t>(0));
  bool consumed_source = false;
  if (frac_source_offset + mixer->pos_filter_width() < source_buffer.length()) {
    // When calling Mix(), we communicate the resampling rate with three parameters. We augment
    // step_size with rate_modulo and denominator arguments that capture the remaining rate
    // component that cannot be expressed by a 19.13 fixed-point step_size. Note: step_size and
    // frac_source_offset use the same format -- they have the same limitations in what they can and
    // cannot communicate.
    //
    // For perfect position accuracy, just as we track incoming/outgoing fractional source offset,
    // we also need to track the ongoing subframe_position_modulo. This is now added to Mix() and
    // maintained across calls, but not initially set to any value other than zero. For now, we are
    // deferring that work, tracking it with MTWN-128.
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
    bool ramping = info.gain.IsRamping();
    if (ramping) {
      info.gain.GetScaleArray(
          info.scale_arr.get(),
          std::min(dest_frames_left - dest_offset, Mixer::Bookkeeping::kScaleArrLen),
          dest_ref_clock_to_integral_dest_frame.rate());
    }

    {
      int32_t raw_source_offset = frac_source_offset.raw_value();
      consumed_source = mixer->Mix(buf, dest_frames_left, &dest_offset, source_buffer.payload(),
                                   source_buffer.length().raw_value(), &raw_source_offset,
                                   cur_mix_job_.accumulate);
      frac_source_offset = FractionalFrames<int32_t>::FromRaw(raw_source_offset);
      cur_mix_job_.usages_mixed.insert_all(source_buffer.usage_mask());
      // The gain for the stream will be any previously applied gain combined with any additional
      // gain that will be applied at this stage. In terms of the applied gain of the mixed stream,
      // we consider that to be the max gain of any single source stream.
      float stream_gain_db = Gain::CombineGains(source_buffer.gain_db(), info.gain.GetGainDb());
      cur_mix_job_.applied_gain_db = std::max(cur_mix_job_.applied_gain_db, stream_gain_db);
    }
    FX_DCHECK(dest_offset <= dest_frames_left);

    // If src is ramping, advance by delta of dest_offset
    if (ramping) {
      info.gain.Advance(dest_offset - prev_dest_offset,
                        dest_ref_clock_to_integral_dest_frame.rate());
    }
  } else {
    // This packet was initially within our mix window. After realigning our sampling point to the
    // nearest dest frame, it is now entirely in the past. This can only occur when down-sampling
    // and is made more likely if the rate conversion ratio is very high. We've already reported
    // a partial underflow when realigning, so just complete the packet and move on to the next.
    consumed_source = true;
  }

  if (consumed_source) {
    FX_DCHECK(frac_source_offset + mixer->pos_filter_width() >= source_buffer.length());
  }

  cur_mix_job_.frames_produced += dest_offset;

  FX_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);
  return consumed_source;
}

// We compose the effects of clock reconciliation into our sample-rate-conversion step size, but
// only for streams that use neither the 'optimal' clock, nor the clock we designate as driving our
// hardware-rate-adjustments. We apply this micro-SRC via an intermediate "slew away the error"
// rate-correction factor driven by a PID control. Why use a PID? Sources do not merely chase the
// other clock's rate -- they chase its position. Note that even if we don't adjust our rate, we
// still want a composed transformation for offsets.
void MixStage::ReconcileClocksAndSetStepSize(StreamHolder holder) {
  TRACE_DURATION("audio", "MixStage::ReconcileClocksAndSetStepSize");

  // UpdateSourceTrans
  //
  // Ensure the mappings from source-frame to source-ref-time and monotonic-time are up-to-date.
  UpdateSourceTrans(*holder.stream, &holder.mixer->bookkeeping());

  // UpdateDestTrans
  //
  // Ensure the mappings from dest-frame to monotonic-time is up-to-date.
  UpdateDestTrans(cur_mix_job_, &holder.mixer->bookkeeping());
  // Much of the below is currently included in UpdateDestTrans; this will be split out.

  // ComposeDestToSource
  //
  // Compose our transformation from destination frames to source fractional frames.

  // SynchronizeClocks
  //
  // Sanity-check: between upstream (source) and downstream (dest) clocks, we have one device clock
  //    and one client clock. (Don't forget about capture mix, where usual roles are reversed.)
  //    We might instead have two device clocks (Linearize or Mix stages); should be same clock.
  // If client clock and device clock differ, we will reconcile them:
  //    For start dest frame, measure [predicted - actual] error (in frac_src) since last mix.
  //    If mix is continuous since then, report the latest error to the AudioClock
  //    AudioClock feeds errors to its internal PID, producing a running correction factor....
  //    ... If clock is hardware-controlling, apply correction factor to the hardware.
  //    ... If clock is optimal, apply correction factor directly to the client clock.
  //    ... Otherwise, expose this error correction factor directly to MixStage

  // ComputeFrameRateConversionRatio
  //
  // If optimal or hardware-controlling clock, just compose formats/rates.
  //    Any position error is being handled by applying error correction respectively to the client
  //    clock or to the clock hardware, so we need not handle this in the SRC.
  // If custom clock, include the correction factor along with the formats/rates.
  // In none of these cases do we directly include the reference-clock-to-monotonic factors.
  //    The adjustments mentioned above account for the clock discrepancies.

  // SetStepSize
  //
  // Convert the TimelineRate into step_size, denominator and rate_modulo -- as usual.
}

void MixStage::UpdateSourceTrans(ReadableStream& stream, Mixer::Bookkeeping* bk) {
  TRACE_DURATION("audio", "MixStage::UpdateSourceTrans");

  auto snapshot = stream.ReferenceClockToFractionalFrames();
  bk->source_ref_clock_to_frac_source_frames = snapshot.timeline_function;

  if (bk->source_ref_clock_to_frac_source_frames.subject_delta() == 0) {
    bk->clock_mono_to_frac_source_frames = TimelineFunction();
    return;
  }

  auto source_ref_clock_to_clock_mono = stream.reference_clock().ref_clock_to_clock_mono();
  auto frac_source_frame_to_clock_mono =
      source_ref_clock_to_clock_mono * bk->source_ref_clock_to_frac_source_frames.Inverse();
  bk->clock_mono_to_frac_source_frames = frac_source_frame_to_clock_mono.Inverse();
}

// For now, we do not compose the effects of clock reconciliation into the SRC step size (we do not
// apply 'micro-SRC' based on clock-rate synchronization).  When enabled, we will only apply
// micro-SRC to streams that use neither the 'optimal' clock, nor the clock we designate as
// driving our hardware-rate-adjustments. Eventually, we will apply micro-SRC to those streams via
// an intermediate "slew away the error" rate-correction factor driven by a PID control.
// Why use a PID? Sources do not simply chase the dest clock's rate -- they chases its position.
// Note that even if we don't adjust our rate, we still want a composed transformation for offsets.
//
// This will be a flag added to AudioClock, rather than a const.
constexpr bool kAssumeOptimalClock = true;

void MixStage::UpdateDestTrans(const MixJob& job, Mixer::Bookkeeping* bk) {
  TRACE_DURATION("audio", "MixStage::UpdateDestTrans");

  // We should only be here if we have a valid mix job. This means a job which supplies a valid
  // transformation from reference time to destination frames (based on dest frame rate).
  FX_DCHECK(job.dest_ref_clock_to_frac_dest_frame.rate().reference_delta());
  FX_DCHECK(job.dest_ref_clock_to_frac_dest_frame.rate().subject_delta());

  // Assert we can map from local monotonic-time to fractional source frames.
  FX_DCHECK(bk->clock_mono_to_frac_source_frames.rate().reference_delta());

  // Don't bother with these calculations, if source-rate numerator is zero (we are stopped).
  if (bk->clock_mono_to_frac_source_frames.subject_delta() == 0) {
    bk->dest_frames_to_frac_source_frames = TimelineFunction();
    bk->step_size = 0;
    bk->denominator = 0;  // we need not also clear rate_mod and pos_mod
    return;
  }

  auto dest_frame_to_dest_ref_clock =
      ReferenceClockToIntegralFrames(job.dest_ref_clock_to_frac_dest_frame).Inverse();

  // Compose our transformation from local monotonic-time to dest frames.
  FX_DCHECK(reference_clock().is_valid());
  auto dest_frames_to_clock_mono =
      reference_clock().ref_clock_to_clock_mono() * dest_frame_to_dest_ref_clock;

  // Combine the job-supplied dest transformation, with the renderer-supplied mapping of
  // monotonic-to-source-subframe, to produce a transformation which maps from dest frames to
  // fractional source frames.
  bk->dest_frames_to_frac_source_frames =
      bk->clock_mono_to_frac_source_frames * dest_frames_to_clock_mono;

  // Finally, compute the step size in subframes. IOW, every time we move forward one dest
  // frame, how many source subframes should we consume.
  TimelineRate frac_src_frames_per_dest_frame;

  if constexpr (kAssumeOptimalClock) {
    // If micro-SRC is disabled, ignore the clock factors (treat src & dest clocks as identical).
    auto dest_frames_to_dest_ref_clock =
        ReferenceClockToIntegralFrames(job.dest_ref_clock_to_frac_dest_frame).Inverse();
    frac_src_frames_per_dest_frame =
        dest_frames_to_dest_ref_clock.rate() * bk->source_ref_clock_to_frac_source_frames.rate();
  } else {
    frac_src_frames_per_dest_frame = bk->dest_frames_to_frac_source_frames.rate();
  }

  FX_DCHECK(frac_src_frames_per_dest_frame.reference_delta());

  int64_t tmp_step_size = frac_src_frames_per_dest_frame.Scale(1);

  FX_DCHECK(tmp_step_size >= 0);
  FX_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());

  bk->step_size = static_cast<uint32_t>(tmp_step_size);
  bk->denominator = frac_src_frames_per_dest_frame.reference_delta();
  bk->rate_modulo =
      frac_src_frames_per_dest_frame.subject_delta() - (bk->denominator * bk->step_size);
}

}  // namespace media::audio
