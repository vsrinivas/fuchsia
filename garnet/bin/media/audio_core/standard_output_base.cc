// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/standard_output_base.h"

#include <fbl/auto_lock.h>
#include <lib/fit/defer.h>
#include <limits>

#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_core/audio_renderer_impl.h"
#include "garnet/bin/media/audio_core/mixer/mixer.h"
#include "garnet/bin/media/audio_core/mixer/no_op.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/time/time_delta.h"

namespace media::audio {

static constexpr fxl::TimeDelta kMaxTrimPeriod =
    fxl::TimeDelta::FromMilliseconds(10);

StandardOutputBase::StandardOutputBase(AudioDeviceManager* manager)
    : AudioOutput(manager) {
  next_sched_time_ = fxl::TimePoint::Now();
  next_sched_time_known_ = true;
  source_link_refs_.reserve(16u);
}

zx_status_t StandardOutputBase::Init() {
  zx_status_t res = AudioOutput::Init();
  if (res != ZX_OK) {
    return res;
  }

  mix_timer_ = ::dispatcher::Timer::Create();
  if (mix_timer_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  ::dispatcher::Timer::ProcessHandler process_handler(
      [output =
           fbl::WrapRefPtr(this)](::dispatcher::Timer* timer) -> zx_status_t {
        OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
        output->Process();
        return ZX_OK;
      });

  res = mix_timer_->Activate(mix_domain_, std::move(process_handler));
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate mix_timer_ (res " << res << ")";
  }

  return res;
}

void StandardOutputBase::Process() {
  bool mixed = false;
  fxl::TimePoint now = fxl::TimePoint::Now();

  // At this point, we should always know when our implementation would like to
  // be called to do some mixing work next.  If we do not know, then we should
  // have already shut down.
  //
  // If the next sched time has not arrived yet, don't attempt to mix anything.
  // Just trim the queues and move on.
  FXL_DCHECK(next_sched_time_known_);
  if (now >= next_sched_time_) {
    // Clear the flag. If the implementation does not set it during the cycle by
    // calling SetNextSchedTime, we consider it an error and shut down.
    next_sched_time_known_ = false;

    // As long as our implementation wants to mix more and has not run into a
    // problem trying to finish the mix job, mix some more.
    do {
      ::memset(&cur_mix_job_, 0, sizeof(cur_mix_job_));

      if (!StartMixJob(&cur_mix_job_, now)) {
        break;
      }

      // If we have a mix job, then we must have an output producer, and an
      // intermediate buffer allocated, and it must be large enough for the mix
      // job we were given.
      FXL_DCHECK(mix_buf_);
      FXL_DCHECK(output_producer_);
      FXL_DCHECK(cur_mix_job_.buf_frames <= mix_buf_frames_);

      // If we are not muted, actually do the mix.  Otherwise, just fill the
      // final buffer with silence.  Do not set the 'mixed' flag if we are
      // muted.  This is our signal that we still need to trim our sources
      // (something that happens automatically if we mix).
      if (!cur_mix_job_.sw_output_muted) {
        // Fill the intermediate buffer with silence.
        size_t bytes_to_zero = sizeof(mix_buf_[0]) * cur_mix_job_.buf_frames *
                               output_producer_->channels();
        ::memset(mix_buf_.get(), 0, bytes_to_zero);

        // Mix each renderer into the intermediate accumulator buffer, then
        // reformat (and clip) into the final output buffer.
        ForeachLink(TaskType::Mix);
        output_producer_->ProduceOutput(mix_buf_.get(), cur_mix_job_.buf,
                                        cur_mix_job_.buf_frames);
        mixed = true;
      } else {
        output_producer_->FillWithSilence(cur_mix_job_.buf,
                                          cur_mix_job_.buf_frames);
      }

    } while (FinishMixJob(cur_mix_job_));
  }

  if (!next_sched_time_known_) {
    FXL_LOG(ERROR) << "Output failed to schedule next service time.  "
                   << "Shutting down!";
    ShutdownSelf();
    return;
  }

  // If we mixed nothing this time, make sure that we trim all of our renderer
  // queues. No matter what is going on with the output hardware, we are not
  // allowed to hold onto the queued data past its presentation time.
  if (!mixed) {
    ForeachLink(TaskType::Trim);
  }

  // Figure out when we should wake up to do more work again.  No matter how
  // long our implementation wants to wait, we need to make sure to wake up and
  // periodically trim our input queues.
  fxl::TimePoint max_sched_time = now + kMaxTrimPeriod;
  if (next_sched_time_ > max_sched_time) {
    next_sched_time_ = max_sched_time;
  }

  zx_time_t next_time =
      static_cast<zx_time_t>(next_sched_time_.ToEpochDelta().ToNanoseconds());
  if (mix_timer_->Arm(next_time) != ZX_OK) {
    ShutdownSelf();
  }
}

zx_status_t StandardOutputBase::InitializeSourceLink(const AudioLinkPtr& link) {
  auto mix_bookkeeping = std::make_unique<Bookkeeping>();

  // For now, refuse to link to anything but a packet source.  This code does
  // not currently know how to properly handle a ring-buffer source.
  if (link->source_type() != AudioLink::SourceType::Packet) {
    return ZX_ERR_INTERNAL;
  }

  // If we have an output, pick a mixer based on the input and output formats.
  // Otherwise, we only need a NoOp mixer (for the time being).
  auto& packet_link = *(static_cast<AudioLinkPacketSource*>(link.get()));
  if (output_producer_) {
    mix_bookkeeping->mixer = Mixer::Select(packet_link.format_info().format(),
                                           *(output_producer_->format()));
  } else {
    mix_bookkeeping->mixer = MixerPtr(new audio::mixer::NoOp());
  }

  if (mix_bookkeeping->mixer == nullptr) {
    FXL_LOG(ERROR) << "*** Audio system mixer cannot convert between formats "
                      "*** (could not select mixer while linking to output). "
                      "Usually, this indicates a 'num_channels' mismatch.";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // The Gain object contains multiple stages. In render, stream gain is
  // "source" gain and device (or system) gain is "dest" gain.
  //
  // The renderer will set this link's source gain once this call returns.
  //
  // Set the dest gain -- device gain retrieved from device settings.
  if (device_settings_ != nullptr) {
    AudioDeviceSettings::GainState cur_gain_state;
    device_settings_->SnapshotGainState(&cur_gain_state);

    mix_bookkeeping->gain.SetDestMute(cur_gain_state.muted);
    mix_bookkeeping->gain.SetDestGain(cur_gain_state.gain_db);
  }
  // Settings should exist but if they don't, we use default DestGain (Unity).

  // Things went well. Stash a reference to our bookkeeping and get out.
  link->set_bookkeeping(std::move(mix_bookkeeping));
  return ZX_OK;
}

void StandardOutputBase::SetupMixBuffer(uint32_t max_mix_frames) {
  FXL_DCHECK(output_producer_->channels() > 0u);
  FXL_DCHECK(max_mix_frames > 0u);
  FXL_DCHECK(static_cast<uint64_t>(max_mix_frames) *
                 output_producer_->channels() <=
             std::numeric_limits<uint32_t>::max());

  mix_buf_frames_ = max_mix_frames;
  mix_buf_.reset(new float[mix_buf_frames_ * output_producer_->channels()]);
}

void StandardOutputBase::ForeachLink(TaskType task_type) {
  // Make a copy of our currently active set of links so that we don't have to
  // hold onto mutex_ for the entire mix operation.
  {
    fbl::AutoLock links_lock(&links_lock_);
    ZX_DEBUG_ASSERT(source_link_refs_.empty());
    for (const auto& link_ptr : source_links_) {
      // For now, skip ring-buffer source links. This code cannot mix them yet.
      if (link_ptr->source_type() != AudioLink::SourceType::Packet) {
        continue;
      }

      source_link_refs_.push_back(link_ptr);
    }
  }

  // In all cases, release our temporary references upon leaving this method.
  auto cleanup = fit::defer(
      [this]() FXL_NO_THREAD_SAFETY_ANALYSIS { source_link_refs_.clear(); });

  for (const auto& link : source_link_refs_) {
    // Quit early if we should be shutting down.
    if (is_shutting_down()) {
      return;
    }

    // Is the link still valid?  If so, process it.
    if (!link->valid()) {
      continue;
    }

    FXL_DCHECK(link->source_type() == AudioLink::SourceType::Packet);
    FXL_DCHECK(link->GetSource()->type() == AudioObject::Type::AudioRenderer);
    auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
    auto audio_renderer =
        fbl::RefPtr<AudioRendererImpl>::Downcast(link->GetSource());

    // It would be nice to be able to use a dynamic cast for this, but currently
    // we are building with no-rtti
    auto* info = static_cast<Bookkeeping*>(packet_link->bookkeeping().get());
    FXL_DCHECK(info);

    // Ensure the mapping from source-frame to local-time is up-to-date.
    UpdateSourceTrans(audio_renderer, info);

    bool setup_done = false;
    fbl::RefPtr<AudioPacketRef> pkt_ref;

    bool release_audio_renderer_packet;
    while (true) {
      release_audio_renderer_packet = false;
      // Try to grab the packet queue's front. If it has been flushed since the
      // last time we grabbed it, reset our mixer's internal filter state.
      bool was_flushed;
      pkt_ref = packet_link->LockPendingQueueFront(&was_flushed);
      if (was_flushed) {
        info->mixer->Reset();
      }

      // If the queue is empty, then we are done.
      if (!pkt_ref) {
        break;
      }

      // If we have not set up for this renderer yet, do so. If the setup
      // fails for any reason, stop processing packets for this renderer.
      if (!setup_done) {
        setup_done = (task_type == TaskType::Mix)
                         ? SetupMix(audio_renderer, info)
                         : SetupTrim(audio_renderer, info);
        if (!setup_done) {
          // Clear our ramps, if we exit with error?
          break;
        }
      }

      // Now process the packet which is at the front of the renderer's queue.
      // If the packet has been entirely consumed, pop it off the front and
      // proceed to the next one. Otherwise, we are finished.
      release_audio_renderer_packet =
          (task_type == TaskType::Mix)
              ? ProcessMix(audio_renderer, info, pkt_ref)
              : ProcessTrim(audio_renderer, info, pkt_ref);

      // If we have mixed enough output frames, we are done with this mix,
      // regardless of what we should now do with the renderer packet.
      if ((task_type == TaskType::Mix) &&
          (cur_mix_job_.frames_produced == cur_mix_job_.buf_frames)) {
        break;
      }
      // If we still need more output, but could not complete this renderer
      // packet (we're paused, or packet is in the future), then we are done.
      if (!release_audio_renderer_packet) {
        break;
      }
      // We did consume this entire renderer packet, and we should keep mixing.
      pkt_ref.reset();
      packet_link->UnlockPendingQueueFront(release_audio_renderer_packet);
    }

    // Unlock queue (completing packet if needed) and proceed to next renderer.
    pkt_ref.reset();
    packet_link->UnlockPendingQueueFront(release_audio_renderer_packet);

    // Note: there is no point in doing this for Trim tasks, but it doesn't hurt
    // anything, and its easier than adding another function to ForeachLink to
    // run after each renderer is processed, just to set this flag.
    cur_mix_job_.accumulate = true;
  }
}

bool StandardOutputBase::SetupMix(
    const fbl::RefPtr<AudioRendererImpl>& audio_renderer, Bookkeeping* info) {
  // If we need to recompose our transformation from output frame space to input
  // fractional frames, do so now.
  FXL_DCHECK(info);
  UpdateDestTrans(cur_mix_job_, info);
  cur_mix_job_.frames_produced = 0;

  return true;
}

bool StandardOutputBase::ProcessMix(
    const fbl::RefPtr<AudioRendererImpl>& audio_renderer, Bookkeeping* info,
    const fbl::RefPtr<AudioPacketRef>& packet) {
  // Bookkeeping should contain: the rechannel matrix (eventually).

  // Sanity check our parameters.
  FXL_DCHECK(info);
  FXL_DCHECK(packet);

  // We had better have a valid job, or why are we here?
  FXL_DCHECK(cur_mix_job_.buf_frames);
  FXL_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);

  // We also must have selected a mixer, or we are in trouble.
  FXL_DCHECK(info->mixer);
  Mixer& mixer = *(info->mixer);

  // If the renderer is currently paused, subject_delta (not just step_size) is
  // zero. This packet may be relevant eventually, but currently it contributes
  // nothing. Tell ForeachLink we are done, but hold the packet for now.
  if (!info->dest_frames_to_frac_source_frames.subject_delta()) {
    return false;
  }

  // Have we produced enough? If so, hold this packet and move to next renderer.
  if (cur_mix_job_.frames_produced >= cur_mix_job_.buf_frames) {
    return false;
  }

  uint32_t frames_left = cur_mix_job_.buf_frames - cur_mix_job_.frames_produced;
  float* buf = mix_buf_.get() +
               (cur_mix_job_.frames_produced * output_producer_->channels());

  // Calculate this job's first and last sampling points, in source sub-frames.
  int64_t first_sample_ftf = info->dest_frames_to_frac_source_frames(
      cur_mix_job_.start_pts_of + cur_mix_job_.frames_produced);

  // Without the "-1", this would be the first output frame of the NEXT job.
  int64_t final_sample_ftf =
      first_sample_ftf +
      info->dest_frames_to_frac_source_frames.rate().Scale(frames_left - 1);

  // If packet has no frames, there's no need to mix it; it may be skipped.
  if (packet->end_pts() == packet->start_pts()) {
    return true;
  }

  // Figure out the PTS of the final frame of audio in our input packet.
  FXL_DCHECK((packet->end_pts() - packet->start_pts()) >= Mixer::FRAC_ONE);
  int64_t final_pts = packet->end_pts() - Mixer::FRAC_ONE;

  // If the PTS of the final frame of audio in our input is before the negative
  // window edge of our filter centered at our first sampling point, then this
  // packet is entirely in the past and may be skipped.
  if (final_pts < (first_sample_ftf - mixer.neg_filter_width())) {
    return true;
  }

  // If the PTS of the first frame of audio in our input is after the positive
  // window edge of our filter centered at our final sampling point, then this
  // packet is entirely in the future and should be held.
  if (packet->start_pts() > (final_sample_ftf + mixer.pos_filter_width())) {
    return false;
  }

  // Evidently this input packet intersects our mixer's filter. Compute where
  // (in the output buffer) our first output sample will land, and where (in the
  // input packet) we should start sampling the input.
  int64_t input_offset_64 = first_sample_ftf - packet->start_pts();
  int64_t output_offset_64 = 0;
  int64_t first_sample_pos_window_edge =
      first_sample_ftf + mixer.pos_filter_width();

  // If the packet's first frame comes after the filter window's positive edge,
  // then we should skip some output frames before starting to produce data.
  if (packet->start_pts() > first_sample_pos_window_edge) {
    const TimelineRate& dest_to_src =
        info->dest_frames_to_frac_source_frames.rate();
    output_offset_64 = dest_to_src.Inverse().Scale(
        packet->start_pts() - first_sample_pos_window_edge + Mixer::FRAC_ONE -
        1);
    input_offset_64 += dest_to_src.Scale(output_offset_64);
  }

  FXL_DCHECK(output_offset_64 >= 0);
  FXL_DCHECK(output_offset_64 < static_cast<int64_t>(frames_left));
  FXL_DCHECK(input_offset_64 <= std::numeric_limits<int32_t>::max());
  FXL_DCHECK(input_offset_64 >= std::numeric_limits<int32_t>::min());

  auto output_offset = static_cast<uint32_t>(output_offset_64);
  auto frac_input_offset = static_cast<int32_t>(input_offset_64);

  // Looks like we are ready to go. Mix.
  FXL_DCHECK(packet->frac_frame_len() <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  bool consumed_source = false;
  if (frac_input_offset < static_cast<int32_t>(packet->frac_frame_len())) {
    // When calling Mix(), we communicate the resampling rate with three
    // parameters. We augment step_size with rate_modulo and denominator
    // arguments that capture the remaining rate component that cannot be
    // expressed by a 19.13 fixed-point step_size. Note: step_size and
    // frac_input_offset use the same format -- they have the same limitations
    // in what they can and cannot communicate.
    //
    // For perfect position accuracy, just as we track incoming/outgoing
    // fractional source offset, we also need to track the ongoing
    // subframe_position_modulo. This is now added to Mix() and maintained
    // across calls, but not initially set to any value other than zero.
    // For now, we are deferring that work, tracking it with MTWN-128.
    //
    // Q: Why did we solve this issue for Rate but not for initial Position?
    // A: We solved this issue for *rate* because its effect accumulates over
    // time, causing clearly measurable distortion that becomes crippling with
    // larger jobs. For *position*, there is no accumulated magnification over
    // time -- in analyzing the distortion that this should cause, mix job
    // size affects the distortion's frequency but not its amplitude. We expect
    // the effects to be below audible thresholds. Until the effects are
    // measurable and attributable to this jitter, we will defer this work.
    //
    // TODO(mpuryear): integrate bookkeeping into the Mixer itself (MTWN-129).

    uint32_t prev_output_offset = output_offset;

    // Check whether we are still ramping
    bool ramping = info->gain.IsRamping();
    if (ramping) {
      info->gain.GetScaleArray(
          info->scale_arr.get(),
          std::min(frames_left - output_offset, Bookkeeping::kScaleArrLen),
          cur_mix_job_.local_to_output->rate());
    }

    consumed_source =
        info->mixer->Mix(buf, frames_left, &output_offset, packet->payload(),
                         packet->frac_frame_len(), &frac_input_offset,
                         cur_mix_job_.accumulate, info);
    FXL_DCHECK(output_offset <= frames_left);

    // If src is ramping, advance by delta of output_offset
    if (ramping) {
      info->gain.Advance(output_offset - prev_output_offset,
                         cur_mix_job_.local_to_output->rate());
    }
  }

  if (consumed_source) {
    FXL_DCHECK(frac_input_offset + info->mixer->pos_filter_width() >=
               packet->frac_frame_len());
  }

  cur_mix_job_.frames_produced += output_offset;

  FXL_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);
  return consumed_source;
}

bool StandardOutputBase::SetupTrim(
    const fbl::RefPtr<AudioRendererImpl>& audio_renderer, Bookkeeping* info) {
  // Compute the cutoff time used to decide whether to trim packets. ForeachLink
  // has already updated our transformation, no need for us to do so here.
  FXL_DCHECK(info);

  int64_t local_now_ticks =
      (fxl::TimePoint::Now() - fxl::TimePoint()).ToNanoseconds();

  // RateControlBase guarantees that the transformation into the media timeline
  // is never singular.  If a forward transformation fails it must be because of
  // overflow, which should be impossible unless user defined a playback rate
  // where the ratio of media-ticks-to-local-ticks is greater than one.
  trim_threshold_ = info->clock_mono_to_frac_source_frames(local_now_ticks);

  return true;
}

bool StandardOutputBase::ProcessTrim(
    const fbl::RefPtr<AudioRendererImpl>& audio_renderer, Bookkeeping* info,
    const fbl::RefPtr<AudioPacketRef>& pkt_ref) {
  FXL_DCHECK(pkt_ref);

  // If the presentation end of this packet is in the future, stop trimming.
  if (pkt_ref->end_pts() > trim_threshold_) {
    return false;
  }

  return true;
}

void StandardOutputBase::UpdateSourceTrans(
    const fbl::RefPtr<AudioRendererImpl>& audio_renderer, Bookkeeping* bk) {
  FXL_DCHECK(audio_renderer != nullptr);
  uint32_t gen = bk->source_trans_gen_id;

  audio_renderer->SnapshotCurrentTimelineFunction(
      Timeline::local_now(), &bk->clock_mono_to_frac_source_frames, &gen);

  // If local->media transformation hasn't changed since last time, we're done.
  if (bk->source_trans_gen_id == gen) {
    return;
  }

  // Transformation has changed. Update gen; invalidate dest-to-src generation.
  bk->source_trans_gen_id = gen;
  bk->dest_trans_gen_id = kInvalidGenerationId;
}

void StandardOutputBase::UpdateDestTrans(const MixJob& job, Bookkeeping* bk) {
  // We should only be here if we have a valid mix job. This means a job which
  // supplies a valid transformation from local time to output frames.
  FXL_DCHECK(job.local_to_output);
  FXL_DCHECK(job.local_to_output_gen != kInvalidGenerationId);

  // If generations match, don't re-compute -- just use what we have already.
  if (bk->dest_trans_gen_id == job.local_to_output_gen) {
    return;
  }

  // Assert we can map from local time to fractional renderer frames.
  FXL_DCHECK(bk->source_trans_gen_id != kInvalidGenerationId);

  // Combine the job-supplied local-to-output transformation, with the
  // renderer-supplied mapping of local-to-input-subframe, to produce a
  // transformation which maps from output frames to fractional input frames.
  TimelineFunction& dest = bk->dest_frames_to_frac_source_frames;
  dest = bk->clock_mono_to_frac_source_frames * job.local_to_output->Inverse();

  // Finally, compute the step size in subframes. IOW, every time we move
  // forward one output frame, how many input subframes should we consume. Don't
  // bother doing the multiplications if already we know the numerator is zero.
  FXL_DCHECK(dest.rate().reference_delta());
  if (!dest.rate().subject_delta()) {
    bk->step_size = 0;
    bk->denominator = 0;  // shouldn't also need to clear rate_mod and pos_mod
  } else {
    int64_t tmp_step_size = dest.rate().Scale(1);

    FXL_DCHECK(tmp_step_size >= 0);
    FXL_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());

    bk->step_size = static_cast<uint32_t>(tmp_step_size);
    bk->denominator = bk->SnapshotDenominatorFromDestTrans();
    bk->rate_modulo =
        dest.rate().subject_delta() - (bk->denominator * bk->step_size);
  }

  // Done, update our dest_trans generation.
  bk->dest_trans_gen_id = job.local_to_output_gen;
}

}  // namespace media::audio
