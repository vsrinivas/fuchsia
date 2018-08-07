// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/standard_output_base.h"

#include <limits>

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>

#include "garnet/bin/media/audio_core/audio_link.h"
#include "garnet/bin/media/audio_core/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_core/audio_renderer_impl.h"
#include "garnet/bin/media/audio_core/mixer/mixer.h"
#include "garnet/bin/media/audio_core/mixer/no_op.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"

namespace media {
namespace audio {

static constexpr fxl::TimeDelta kMaxTrimPeriod =
    fxl::TimeDelta::FromMilliseconds(10);

StandardOutputBase::RendererBookkeeping::RendererBookkeeping() {}
StandardOutputBase::RendererBookkeeping::~RendererBookkeeping() {}

StandardOutputBase::StandardOutputBase(AudioDeviceManager* manager)
    : AudioOutput(manager) {
  next_sched_time_ = fxl::TimePoint::Now();
  next_sched_time_known_ = true;
  source_link_refs_.reserve(16u);
}

StandardOutputBase::~StandardOutputBase() {}

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

  res = mix_timer_->Activate(mix_domain_, fbl::move(process_handler));
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
    // Clear the flag, if the implementation does not set this flag by calling
    // SetNextSchedTime during the cycle, we consider it to be an error and shut
    // down.
    next_sched_time_known_ = false;

    // As long as our implementation wants to mix more and has not run into a
    // problem trying to finish the mix job, mix some more.
    do {
      ::memset(&cur_mix_job_, 0, sizeof(cur_mix_job_));

      if (!StartMixJob(&cur_mix_job_, now)) {
        break;
      }

      // If we have a mix job, then we must have an output formatter, and an
      // intermediate buffer allocated, and it must be large enough for the mix
      // job we were given.
      FXL_DCHECK(mix_buf_);
      FXL_DCHECK(output_formatter_);
      FXL_DCHECK(cur_mix_job_.buf_frames <= mix_buf_frames_);

      // If we are not muted, actually do the mix.  Otherwise, just fill the
      // final buffer with silence.  Do not set the 'mixed' flag if we are
      // muted.  This is our signal that we still need to trim our sources
      // (something that happens automatically if we mix).
      if (!cur_mix_job_.sw_output_muted) {
        // Fill the intermediate buffer with silence.
        size_t bytes_to_zero = sizeof(mix_buf_[0]) * cur_mix_job_.buf_frames *
                               output_formatter_->channels();
        ::memset(mix_buf_.get(), 0, bytes_to_zero);

        // Mix each renderer into the intermediate buffer, then clip/format into
        // the final buffer.
        ForeachLink(TaskType::Mix);
        output_formatter_->ProduceOutput(mix_buf_.get(), cur_mix_job_.buf,
                                         cur_mix_job_.buf_frames);
        mixed = true;
      } else {
        output_formatter_->FillWithSilence(cur_mix_job_.buf,
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
  // queues.  No matter what is going on with the output hardware, we are not
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
  if (mix_timer_->Arm(next_time)) {
    ShutdownSelf();
  }
}

zx_status_t StandardOutputBase::InitializeSourceLink(const AudioLinkPtr& link) {
  RendererBookkeeping* bk = AllocBookkeeping();
  std::unique_ptr<AudioLink::Bookkeeping> ref(bk);

  // We should never fail to allocate our bookkeeping.  The only way this can
  // happen is if we have a badly behaved implementation.
  if (!bk) {
    return ZX_ERR_INTERNAL;
  }

  // For now, refuse to link to anything but a packet source.  This code does
  // not currently know how to properly handle a ring-buffer source.
  if (link->source_type() != AudioLink::SourceType::Packet) {
    return ZX_ERR_INTERNAL;
  }

  // If we have an output, pick a mixer based on the input and output formats.
  // Otherwise, we only need a NoOp mixer (for the time being).
  auto& packet_link = *(static_cast<AudioLinkPacketSource*>(link.get()));
  if (output_formatter_) {
    bk->mixer = Mixer::Select(packet_link.format_info().format(),
                              *(output_formatter_->format()));
  } else {
    bk->mixer = MixerPtr(new audio::mixer::NoOp());
  }

  if (bk->mixer == nullptr) {
    FXL_LOG(ERROR) << "*** Audio system mixer cannot convert between formats "
                      "*** (could not select mixer while linking to output). "
                      "Usually, this indicates a 'num_channels' mismatch.";
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Looks like things went well.  Stash a reference to our bookkeeping and get
  // out.
  link->set_bookkeeping(std::move(ref));
  return ZX_OK;
}

StandardOutputBase::RendererBookkeeping*
StandardOutputBase::AllocBookkeeping() {
  return new RendererBookkeeping();
}

void StandardOutputBase::SetupMixBuffer(uint32_t max_mix_frames) {
  FXL_DCHECK(output_formatter_->channels() > 0u);
  FXL_DCHECK(max_mix_frames > 0u);
  FXL_DCHECK(max_mix_frames <= std::numeric_limits<uint32_t>::max() /
                                   output_formatter_->channels());

  mix_buf_frames_ = max_mix_frames;
  mix_buf_.reset(new float[mix_buf_frames_ * output_formatter_->channels()]);
}

void StandardOutputBase::ForeachLink(TaskType task_type) {
  // Make a copy of our currently active set of links so that we don't have to
  // hold onto mutex_ for the entire mix operation.
  {
    fbl::AutoLock links_lock(&links_lock_);
    ZX_DEBUG_ASSERT(source_link_refs_.empty());
    for (const auto& link_ptr : source_links_) {
      // For now, skip ring-buffer source links.  This code does not know how to
      // mix them yet.
      if (link_ptr->source_type() != AudioLink::SourceType::Packet) {
        continue;
      }

      source_link_refs_.push_back(link_ptr);
    }
  }

  // No matter what happens, make sure we release our temporary references as
  // soons a we exit this method.
  auto cleanup = fbl::MakeAutoCall(
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
    FXL_DCHECK(link->GetSource()->type() == AudioObject::Type::Renderer);
    auto packet_link = static_cast<AudioLinkPacketSource*>(link.get());
    auto renderer = fbl::RefPtr<AudioRendererImpl>::Downcast(link->GetSource());

    // It would be nice to be able to use a dynamic cast for this, but currently
    // we are building with no-rtti
    RendererBookkeeping* info =
        static_cast<RendererBookkeeping*>(packet_link->bookkeeping().get());
    FXL_DCHECK(info);

    // Make sure that the mapping between the renderer's frame time domain and
    // local time is up to date.
    info->UpdateRendererTrans(renderer, packet_link->format_info());

    bool setup_done = false;
    fbl::RefPtr<AudioPacketRef> pkt_ref;

    bool release_renderer_packet;
    while (true) {
      release_renderer_packet = false;
      // Try to grab the front of the packet queue.  If it has been flushed
      // since the last time we grabbed it, be sure to reset our mixer's
      // internal filter state.
      bool was_flushed;
      pkt_ref = packet_link->LockPendingQueueFront(&was_flushed);
      if (was_flushed) {
        info->mixer->Reset();
      }

      // If the queue is empty, then we are done.
      if (!pkt_ref) {
        break;
      }

      // If we have not set up for this renderer yet, do so.  If the setup fails
      // for any reason, stop processing packets for this renderer.
      if (!setup_done) {
        setup_done = (task_type == TaskType::Mix) ? SetupMix(renderer, info)
                                                  : SetupTrim(renderer, info);
        if (!setup_done) {
          break;
        }
      }

      // Capture the amplitude to apply for the next bit of audio, recomputing
      // as needed.
      if (task_type == TaskType::Mix) {
        info->amplitude_scale =
            packet_link->gain().GetGainScale(cur_mix_job_.sw_output_db_gain);
      }

      // Now process the packet which is at the front of the renderer's queue.
      // If the packet has been entirely consumed, pop it off the front and
      // proceed to the next one.  Otherwise, we are finished.
      release_renderer_packet = (task_type == TaskType::Mix)
                                    ? ProcessMix(renderer, info, pkt_ref)
                                    : ProcessTrim(renderer, info, pkt_ref);

      // If we are mixing, and we have produced enough output frames, then we
      // are done with this mix, regardless of what we should now do with the
      // renderer packet.
      if ((task_type == TaskType::Mix) &&
          (cur_mix_job_.frames_produced == cur_mix_job_.buf_frames)) {
        break;
      }
      // If we still need more output, but could not complete this renderer
      // packet (we're paused, or packet is in the future), then we are done.
      if (!release_renderer_packet) {
        break;
      }
      // We did consume this entire renderer packet, and we should keep mixing.
      pkt_ref.reset();
      packet_link->UnlockPendingQueueFront(release_renderer_packet);
    }

    // Unlock queue (completing packet if needed) and proceed to next renderer.
    pkt_ref.reset();
    packet_link->UnlockPendingQueueFront(release_renderer_packet);

    // Note: there is no point in doing this for the trim task, but it doesn't
    // hurt anything, and its easier then introducing another function to the
    // ForeachLink arguments to run after each renderer is processed just for
    // the purpose of setting this flag.
    cur_mix_job_.accumulate = true;
  }
}

bool StandardOutputBase::SetupMix(
    const fbl::RefPtr<AudioRendererImpl>& renderer, RendererBookkeeping* info) {
  // If we need to recompose our transformation from output frame space to input
  // fractional frames, do so now.
  FXL_DCHECK(info);
  info->UpdateOutputTrans(cur_mix_job_);
  cur_mix_job_.frames_produced = 0;

  return true;
}

bool StandardOutputBase::ProcessMix(
    const fbl::RefPtr<AudioRendererImpl>& renderer, RendererBookkeeping* info,
    const fbl::RefPtr<AudioPacketRef>& packet) {
  // Sanity check our parameters.
  FXL_DCHECK(info);
  FXL_DCHECK(packet);

  // We had better have a valid job, or why are we here?
  FXL_DCHECK(cur_mix_job_.buf_frames);
  FXL_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);

  // We also must have selected a mixer, or we are in trouble.
  FXL_DCHECK(info->mixer);
  Mixer& mixer = *(info->mixer);

  // If this renderer is currently paused, our subject_delta (not just our
  // step_size) will be zero.  This packet may be relevant at some point in the
  // future, but right now it contributes nothing.  Tell the ForeachLink loop
  // that we are done and to hold onto this packet for now.
  if (!info->output_frames_to_renderer_subframes.subject_delta()) {
    return false;
  }

  // Have we produced all that we are supposed to?  If so, hold the current
  // packet and move on to the next renderer.
  if (cur_mix_job_.frames_produced >= cur_mix_job_.buf_frames) {
    return false;
  }

  uint32_t frames_left = cur_mix_job_.buf_frames - cur_mix_job_.frames_produced;
  float* buf = mix_buf_.get() +
               (cur_mix_job_.frames_produced * output_formatter_->channels());

  // Figure out where the first and last sampling points of this job are,
  // expressed in fractional renderer frames.
  int64_t first_sample_ftf = info->output_frames_to_renderer_subframes(
      cur_mix_job_.start_pts_of + cur_mix_job_.frames_produced);
  // Without the "-1", this would be the first output frame of the NEXT job.
  int64_t final_sample_ftf =
      first_sample_ftf +
      info->output_frames_to_renderer_subframes.rate().Scale(frames_left - 1);

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

  // Looks like the contents of this input packet intersect our mixer's filter.
  // Compute where in the output buffer the first sample will be produced, as
  // well as where, relative to the start of the input packet, this sample will
  // be taken from.
  int64_t input_offset_64 = first_sample_ftf - packet->start_pts();
  int64_t output_offset_64 = 0;
  int64_t first_sample_pos_window_edge =
      first_sample_ftf + mixer.pos_filter_width();

  // If the first frame in this packet comes after the positive edge of the
  // filter window, then we need to skip some number of output frames before
  // starting to produce data.
  if (packet->start_pts() > first_sample_pos_window_edge) {
    const TimelineRate& dst_to_src =
        info->output_frames_to_renderer_subframes.rate();
    output_offset_64 = dst_to_src.Inverse().Scale(packet->start_pts() -
                                                  first_sample_pos_window_edge +
                                                  Mixer::FRAC_ONE - 1);
    input_offset_64 += dst_to_src.Scale(output_offset_64);
  }

  FXL_DCHECK(output_offset_64 >= 0);
  FXL_DCHECK(output_offset_64 < static_cast<int64_t>(frames_left));
  FXL_DCHECK(input_offset_64 <= std::numeric_limits<int32_t>::max());
  FXL_DCHECK(input_offset_64 >= std::numeric_limits<int32_t>::min());

  uint32_t output_offset = static_cast<uint32_t>(output_offset_64);
  int32_t frac_input_offset = static_cast<int32_t>(input_offset_64);

  // Looks like we are ready to go. Mix.
  FXL_DCHECK(packet->frac_frame_len() <=
             static_cast<uint32_t>(std::numeric_limits<int32_t>::max()));

  bool consumed_source = false;
  if (frac_input_offset < static_cast<int32_t>(packet->frac_frame_len())) {
    // When calling Mix(), we communicate the resampling rate with three
    // parameters. We augment frac_step_size with modulo and denominator
    // arguments that capture the remaining rate component that cannot be
    // expressed by a 19.13 fixed-point step_size. Note: frac_step_size and
    // frac_input_offset use the same format -- they have the same limitations
    // in what they can and cannot communicate. This begs two questions:
    //
    // Q1: For perfect position accuracy, don't we also need an in/out param
    // to specify initial/final subframe modulo, for fractional source offset?
    // A1: Yes, for optimum position accuracy (within quantization limits), we
    // SHOULD incorporate running subframe position_modulo in this way.
    //
    // For now, we are defering this work, tracking it with MTWN-128.
    //
    // Q2: Why did we solve this issue for rate but not for initial position?
    // A2: We solved this issue for *rate* because its effect accumulates over
    // time, causing clearly measurable distortion that becomes crippling with
    // larger jobs. For *position*, there is no accumulated magnification over
    // time -- in analyzing the distortion that this should cause, mix job
    // size would affect the distortion frequency but not amplitude. We expect
    // the effects to be below audible thresholds. Until the effects are
    // measurable and attributable to this jitter, we will defer this work.

    // TODO(mpuryear): integrate bookkeeping into the Mixer itself (MTWN-129).
    consumed_source = info->mixer->Mix(
        buf, frames_left, &output_offset, packet->payload(),
        packet->frac_frame_len(), &frac_input_offset, info->step_size,
        info->amplitude_scale, cur_mix_job_.accumulate, info->modulo,
        info->denominator());
    FXL_DCHECK(output_offset <= frames_left);
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
    const fbl::RefPtr<AudioRendererImpl>& renderer, RendererBookkeeping* info) {
  // Compute the cutoff time we will use to decide wether or not to trim
  // packets.  ForeachLink has already updated our transformation, no need
  // for us to do so here.
  FXL_DCHECK(info);

  int64_t local_now_ticks =
      (fxl::TimePoint::Now() - fxl::TimePoint()).ToNanoseconds();

  // The behavior of the RateControlBase implementation guarantees that the
  // transformation into the media timeline is never singular.  If the
  // forward transformation fails it can only be because of an overflow,
  // which should be impossible unless the user has defined a playback rate
  // where the ratio between media time ticks and local time ticks is
  // greater than one.
  trim_threshold_ = info->local_time_to_renderer_subframes(local_now_ticks);

  return true;
}

bool StandardOutputBase::ProcessTrim(
    const fbl::RefPtr<AudioRendererImpl>& renderer, RendererBookkeeping* info,
    const fbl::RefPtr<AudioPacketRef>& pkt_ref) {
  FXL_DCHECK(pkt_ref);

  // If the presentation end of this packet is in the future, stop trimming.
  if (pkt_ref->end_pts() > trim_threshold_) {
    return false;
  }

  return true;
}

void StandardOutputBase::RendererBookkeeping::UpdateRendererTrans(
    const fbl::RefPtr<AudioRendererImpl>& renderer,
    const AudioRendererFormatInfo& format_info) {
  FXL_DCHECK(renderer != nullptr);
  TimelineFunction timeline_function;
  uint32_t gen = local_time_to_renderer_subframes_gen;

  renderer->SnapshotCurrentTimelineFunction(
      Timeline::local_now(), &local_time_to_renderer_subframes, &gen);

  // If the local time -> media time transformation has not changed since the
  // last time we examined it, just get out now.
  if (local_time_to_renderer_subframes_gen == gen) {
    return;
  }

  // The transformation has changed, re-compute the local time -> renderer frame
  // transformation.
  local_time_to_renderer_frames =
      local_time_to_renderer_subframes *
      TimelineFunction(TimelineRate(1u, 1u << kPtsFractionalBits));

  // Update the generation, and invalidate the output to renderer generation.
  local_time_to_renderer_subframes_gen = gen;
  out_frames_to_renderer_subframes_gen = kInvalidGenerationId;
}

void StandardOutputBase::RendererBookkeeping::UpdateOutputTrans(
    const MixJob& job) {
  // We should not be here unless we have a valid mix job.  From our point of
  // view, this means that we have a job which supplies a valid transformation
  // from local time to output frames.
  FXL_DCHECK(job.local_to_output);
  FXL_DCHECK(job.local_to_output_gen != kInvalidGenerationId);

  // If our generations match, we don't need to re-compute anything.  Just use
  // what we have already.
  if (out_frames_to_renderer_subframes_gen == job.local_to_output_gen) {
    return;
  }

  // Assert that we have a good mapping from local time to fractional renderer
  // frames.
  //
  // TODO(johngro): Don't assume that 0 means invalid.  Make it a proper
  // constant defined somewhere.
  FXL_DCHECK(local_time_to_renderer_subframes_gen);

  output_frames_to_renderer_frames =
      local_time_to_renderer_frames * job.local_to_output->Inverse();

  // Compose the job supplied transformation from local to output with the
  // renderer supplied mapping from local to fraction input frames to produce a
  // transformation which maps from output frames to fractional input frames.
  TimelineFunction& dst = output_frames_to_renderer_subframes;

  dst = local_time_to_renderer_subframes * job.local_to_output->Inverse();

  // Finally, compute the step size in fractional frames.  IOW, every time
  // we move forward one output frame, how many fractional frames of input
  // do we consume.  Don't bother doing the multiplication if we already
  // know that the numerator is zero.
  FXL_DCHECK(dst.rate().reference_delta());
  if (!dst.rate().subject_delta()) {
    step_size = 0;
    modulo = 0;
  } else {
    int64_t tmp_step_size = dst.rate().Scale(1);

    FXL_DCHECK(tmp_step_size >= 0);
    FXL_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());

    step_size = static_cast<uint32_t>(tmp_step_size);
    modulo = dst.rate().subject_delta() - (denominator() * step_size);
  }

  // Done, update our generation.
  out_frames_to_renderer_subframes_gen = job.local_to_output_gen;
}

}  // namespace audio
}  // namespace media
