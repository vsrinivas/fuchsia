// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/platform/generic/standard_output_base.h"

#include <fbl/auto_call.h>
#include <limits>

#include "garnet/bin/media/audio_server/audio_renderer_format_info.h"
#include "garnet/bin/media/audio_server/audio_renderer_impl.h"
#include "garnet/bin/media/audio_server/audio_renderer_to_output_link.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/media/flog/flog.h"

namespace media {
namespace audio {

static constexpr fxl::TimeDelta kMaxTrimPeriod =
    fxl::TimeDelta::FromMilliseconds(10);
constexpr uint32_t StandardOutputBase::MixJob::kInvalidGeneration;

StandardOutputBase::RendererBookkeeping::RendererBookkeeping() {}
StandardOutputBase::RendererBookkeeping::~RendererBookkeeping() {}

StandardOutputBase::StandardOutputBase(AudioOutputManager* manager)
    : AudioOutput(manager) {
  next_sched_time_ = fxl::TimePoint::Now();
  next_sched_time_known_ = true;
  link_refs_.reserve(16u);
}

StandardOutputBase::~StandardOutputBase() {}

MediaResult StandardOutputBase::Init() {
  MediaResult res = AudioOutput::Init();
  if (res != MediaResult::OK) {
    return res;
  }

  mix_timer_ = ::audio::dispatcher::Timer::Create();
  if (mix_timer_ == nullptr) {
    return MediaResult::INSUFFICIENT_RESOURCES;
  }

  // clang-format off
  ::audio::dispatcher::Timer::ProcessHandler process_handler(
    [ output = fbl::WrapRefPtr(this) ]
    (::audio::dispatcher::Timer * timer) -> zx_status_t {
      OBTAIN_EXECUTION_DOMAIN_TOKEN(token, output->mix_domain_);
      output->Process();
      return ZX_OK;
    });
  // clang-format on

  zx_status_t zx_res =
      mix_timer_->Activate(mix_domain_, fbl::move(process_handler));
  if (zx_res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to activate mix_timer_ (res " << res << ")";
    return MediaResult::INTERNAL_ERROR;
  }

  return MediaResult::OK;
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

      // Fill the intermediate buffer with silence.
      size_t bytes_to_zero = sizeof(int32_t) * cur_mix_job_.buf_frames *
                             output_formatter_->channels();
      ::memset(mix_buf_.get(), 0, bytes_to_zero);

      // Mix each renderer into the intermediate buffer, then clip/format into
      // the final buffer.
      ForeachRenderer(TaskType::Mix);
      output_formatter_->ProduceOutput(mix_buf_.get(), cur_mix_job_.buf,
                                       cur_mix_job_.buf_frames);

      mixed = true;
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
    ForeachRenderer(TaskType::Trim);
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

MediaResult StandardOutputBase::InitializeLink(
    const AudioRendererToOutputLinkPtr& link) {
  RendererBookkeeping* bk = AllocBookkeeping();
  AudioRendererToOutputLink::BookkeepingPtr ref(bk);

  // We should never fail to allocate our bookkeeping.  The only way this can
  // happen is if we have a badly behaved implementation.
  if (!bk) {
    return MediaResult::INTERNAL_ERROR;
  }

  // Pick a mixer based on the input and output formats.
  bk->mixer =
      Mixer::Select(link->format_info().format(),
                    output_formatter_ ? &output_formatter_->format() : nullptr);
  if (bk->mixer == nullptr) {
    return MediaResult::UNSUPPORTED_CONFIG;
  }

  // Looks like things went well.  Stash a reference to our bookkeeping and get
  // out.
  link->output_bookkeeping() = std::move(ref);
  return MediaResult::OK;
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
  mix_buf_.reset(new int32_t[mix_buf_frames_ * output_formatter_->channels()]);
}

void StandardOutputBase::ForeachRenderer(TaskType task_type) {
  // Make a copy of our currently active set of links so that we don't have to
  // hold onto mutex_ for the entire mix operation.
  {
    fxl::MutexLocker locker(&mutex_);
    ZX_DEBUG_ASSERT(link_refs_.empty());
    for (auto iter = links_.begin(); iter != links_.end();) {
      auto& link = *iter;
      auto tmp_iter = iter++;

      // TODO(johngro) : remove the entire concept of active vs. inactive links.
      // We do not hold the set of active renderer links for very long at all
      // anymore, when a link becomes de-activated, it should just be atomically
      // removed from the set.
      if (!link->active()) {
        links_.erase(tmp_iter);
      } else {
        link_refs_.push_back(link);
      }
    }
  }

  // No matter what happens, make sure we release our temporary references as
  // soons a we exit this method.
  auto cleanup = fbl::MakeAutoCall(
      [this]() FXL_NO_THREAD_SAFETY_ANALYSIS { link_refs_.clear(); });

  for (const auto& link : link_refs_) {
    // Quit early if we should be shutting down.
    if (shutting_down()) {
      return;
    }

    // Is the link's renderer still around?  If so, process it.  Otherwise,
    // remove the renderer entry and move on.
    AudioRendererImplPtr renderer(link->GetRenderer());
    if (renderer == nullptr) {
      continue;
    }

    // It would be nice to be able to use a dynamic cast for this, but currently
    // we are building with no-rtti
    RendererBookkeeping* info =
        static_cast<RendererBookkeeping*>(link->output_bookkeeping().get());
    FXL_DCHECK(info);

    // Make sure that the mapping between the renderer's frame time domain and
    // local time is up to date.
    info->UpdateRendererTrans(renderer, link->format_info());

    bool setup_done = false;
    AudioPipe::AudioPacketRefPtr pkt_ref;

#ifdef FLOG_ENABLED
    if (task_type == TaskType::Mix) {
      setup_done = SetupMix(renderer, info);
      if (!setup_done)
        return;

      // Just starting the job. Report consumption.
      renderer->OnRenderRange(
          info->output_frames_to_renderer_frames(cur_mix_job_.start_pts_of),
          cur_mix_job_.buf_frames *
              info->output_frames_to_renderer_frames.rate());
    }
#endif

    while (true) {
      // Try to grab the front of the packet queue.  If it has been flushed
      // since the last time we grabbed it, be sure to reset our mixer's
      // internal filter state.
      bool was_flushed;
      pkt_ref = link->LockPendingQueueFront(&was_flushed);
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
      info->amplitude_scale = link->gain().GetGainScale(db_gain());

      // Now process the packet which is at the front of the renderer's queue.
      // If the packet has been entirely consumed, pop it off the front and
      // proceed to the next one.  Otherwise, we are finished.
      bool process_result = (task_type == TaskType::Mix)
                                ? ProcessMix(renderer, info, pkt_ref)
                                : ProcessTrim(renderer, info, pkt_ref);
      if (!process_result) {
        break;
      }
      link->UnlockPendingQueueFront(&pkt_ref, true);
    }

    // Unlock the queue and proceed to the next renderer.
    link->UnlockPendingQueueFront(&pkt_ref, false);

    // Note: there is no point in doing this for the trim task, but it dosn't
    // hurt anything, and its easier then introducing another function to the
    // ForeachRenderer arguments to run after each renderer is processed just
    // for the purpose of setting this flag.
    cur_mix_job_.accumulate = true;
  }
}

bool StandardOutputBase::SetupMix(const AudioRendererImplPtr& renderer,
                                  RendererBookkeeping* info) {
  // If we need to recompose our transformation from output frame space to input
  // fractional frames, do so now.
  FXL_DCHECK(info);
  info->UpdateOutputTrans(cur_mix_job_);
  cur_mix_job_.frames_produced = 0;

  return true;
}

bool StandardOutputBase::ProcessMix(
    const AudioRendererImplPtr& renderer,
    RendererBookkeeping* info,
    const AudioPipe::AudioPacketRefPtr& packet) {
  // Sanity check our parameters.
  FXL_DCHECK(info);
  FXL_DCHECK(packet);

  // We had better have a valid job, or why are we here?
  FXL_DCHECK(cur_mix_job_.buf_frames);
  FXL_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);

  // We also must have selected a mixer, or we are in trouble.
  FXL_DCHECK(info->mixer);
  Mixer& mixer = *(info->mixer);

  // If this renderer is currently paused (or being sampled extremely slowly),
  // our step size will be zero.  We know that this packet will be relevant at
  // some point in the future, but right now it contributes nothing.  Tell the
  // ForeachRenderer loop that we are done and to hold onto this packet for now.
  if (!info->step_size) {
    return false;
  }

  // Have we produced all that we are supposed to?  If so, hold the current
  // packet and move on to the next renderer.
  if (cur_mix_job_.frames_produced >= cur_mix_job_.buf_frames) {
    return false;
  }

  uint32_t frames_left = cur_mix_job_.buf_frames - cur_mix_job_.frames_produced;
  int32_t* buf = mix_buf_.get() +
                 (cur_mix_job_.frames_produced * output_formatter_->channels());

  // Figure out where the first and last sampling points of this job are,
  // expressed in fractional renderer frames.
  int64_t first_sample_ftf = info->output_frames_to_renderer_subframes(
      cur_mix_job_.start_pts_of + cur_mix_job_.frames_produced);

  FXL_DCHECK(frames_left);
  int64_t final_sample_ftf =
      first_sample_ftf +
      ((frames_left - 1) * static_cast<int64_t>(info->step_size));

  // If the packet has no frames, there's no need to mix it and it may be
  // skipped.
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
    output_offset_64 = (packet->start_pts() - first_sample_pos_window_edge +
                        info->step_size - 1) /
                       info->step_size;
    input_offset_64 += output_offset_64 * info->step_size;
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

  if (frac_input_offset >= static_cast<int32_t>(packet->frac_frame_len())) {
    frac_input_offset -= packet->frac_frame_len();
  } else {
    bool consumed_source = info->mixer->Mix(
        buf, frames_left, &output_offset, packet->supplied_packet()->payload(),
        packet->frac_frame_len(), &frac_input_offset, info->step_size,
        info->amplitude_scale, cur_mix_job_.accumulate);
    FXL_DCHECK(output_offset <= frames_left);

    if (!consumed_source) {
      // Looks like we didn't consume all of this region.  Assert that we have
      // produced all of our frames and we are done.
      FXL_DCHECK(output_offset == frames_left);
      return false;
    }

    frac_input_offset -= packet->frac_frame_len();
  }

  cur_mix_job_.frames_produced += output_offset;
  FXL_DCHECK(cur_mix_job_.frames_produced <= cur_mix_job_.buf_frames);
  return true;
}

bool StandardOutputBase::SetupTrim(const AudioRendererImplPtr& renderer,
                                   RendererBookkeeping* info) {
  // Compute the cutoff time we will use to decide wether or not to trim
  // packets.  ForeachRenderers has already updated our transformation, no need
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
    const AudioRendererImplPtr& renderer,
    RendererBookkeeping* info,
    const AudioPipe::AudioPacketRefPtr& pkt_ref) {
  FXL_DCHECK(pkt_ref);

  // If the presentation end of this packet is in the future, stop trimming.
  if (pkt_ref->end_pts() > trim_threshold_) {
    return false;
  }

  return true;
}

void StandardOutputBase::RendererBookkeeping::UpdateRendererTrans(
    const AudioRendererImplPtr& renderer,
    const AudioRendererFormatInfo& format_info) {
  FXL_DCHECK(renderer != nullptr);
  TimelineFunction timeline_function;
  uint32_t gen;

  renderer->timeline_control_point().SnapshotCurrentFunction(
      Timeline::local_now(), &timeline_function, &gen);

  // If the local time -> media time transformation has not changed since the
  // last time we examines it, just get out now.
  if (local_time_to_renderer_subframes_gen == gen) {
    return;
  }

  // The control point works in ns units. We want the rate in frames per
  // nanosecond, so we convert here.
  TimelineRate rate_in_frames_per_ns =
      timeline_function.rate() * format_info.frames_per_ns();

  local_time_to_renderer_frames = TimelineFunction(
      timeline_function.reference_time(),
      timeline_function.subject_time() * format_info.frames_per_ns(),
      rate_in_frames_per_ns.reference_delta(),
      rate_in_frames_per_ns.subject_delta());

  // The transformation has changed, re-compute the local time -> renderer frame
  // transformation.
  local_time_to_renderer_subframes =
      TimelineFunction(format_info.frame_to_media_ratio()) *
      local_time_to_renderer_frames;

  // Update the generation, and invalidate the output to renderer generation.
  local_time_to_renderer_subframes_gen = gen;
  out_frames_to_renderer_subframes_gen = MixJob::kInvalidGeneration;
}

void StandardOutputBase::RendererBookkeeping::UpdateOutputTrans(
    const MixJob& job) {
  // We should not be here unless we have a valid mix job.  From our point of
  // view, this means that we have a job which supplies a valid transformation
  // from local time to output frames.
  FXL_DCHECK(job.local_to_output);
  FXL_DCHECK(job.local_to_output_gen != MixJob::kInvalidGeneration);

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
  } else {
    int64_t tmp_step_size = dst.rate().Scale(1);

    FXL_DCHECK(tmp_step_size >= 0);
    FXL_DCHECK(tmp_step_size <= std::numeric_limits<uint32_t>::max());

    step_size = static_cast<uint32_t>(tmp_step_size);
  }

  // Done, update our generation.
  out_frames_to_renderer_subframes_gen = job.local_to_output_gen;
}

}  // namespace audio
}  // namespace media
