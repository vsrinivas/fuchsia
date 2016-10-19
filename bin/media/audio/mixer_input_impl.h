// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <limits>
#include <memory>

#include "apps/media/src/audio/mixdown_table.h"
#include "apps/media/src/audio/mixer_input.h"
#include "apps/media/src/framework/packet.h"
#include "apps/media/src/util/priority_queue_of_unique_ptr.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

// Mixes a stream of packets into output buffers.
template <typename TInSample, typename TOutSample, typename TLevel>
class MixerInputImpl : public MixerInput<TOutSample> {
 public:
  static std::shared_ptr<MixerInputImpl> Create(uint32_t in_channel_count,
                                                uint32_t out_channel_count,
                                                int64_t first_pts) {
    FTL_DCHECK(in_channel_count != 0);
    FTL_DCHECK(out_channel_count != 0);
    return std::shared_ptr<MixerInputImpl>(
        new MixerInputImpl(in_channel_count, out_channel_count, first_pts));
  }

  // Schedules a level change to the values given by the mixdown table to
  // occur at the specified PTS. If fade is false, the level transition is
  // instantaneous. If fade is true, the level transition is a linear fade
  // from the previous mixdown change.
  void AddMixdownScheduleEntry(
      std::unique_ptr<MixdownTable<TLevel>> mixdown_table,
      int64_t pts,
      bool fade);

  // Supplies a packet to be mixed.
  void SupplyPacket(PacketPtr packet) {
    packets_.push_front(std::move(packet));
    if (!current_packet_) {
      OnPacketTransition(next_pts_);
    }
  }

  // MixerInput implementation.
  bool Mix(TOutSample* out_buffer,
           uint32_t out_frame_count,
           int64_t pts) override;

 private:
  MixerInputImpl(uint32_t in_channel_count,
                 uint32_t out_channel_count,
                 int64_t first_pts);

  struct MixdownScheduleEntry {
    std::unique_ptr<MixdownTable<TLevel>> table_;
    int64_t pts_;
    bool fade_;

    MixdownScheduleEntry(std::unique_ptr<MixdownTable<TLevel>> mixdown_table,
                         int64_t pts,
                         bool fade)
        : table_(std::move(mixdown_table)), pts_(pts), fade_(fade) {}

    // This overload is defined in order to get the right entry ordering in
    // mixdown_table_. It doesn't distinguish entries that have the save the
    // same PTS.
    bool operator<(const MixdownScheduleEntry& other) {
      return pts_ < other.pts_;
    }
  };

  struct Job {
    uint32_t in_channel_;
    uint32_t out_channel_;
    Level<TLevel> from_level_ = Level<TLevel>::Silence;
    Level<TLevel> to_level_ = Level<TLevel>::Silence;
    int64_t from_pts_;
    int64_t to_pts_;

    // For intrusive list implementation.
    Job* frontward_ = nullptr;
    Job* backward_ = nullptr;

    // Determines if this Job produces anything but silence.
    bool active() const {
      return from_level_ != Level<TLevel>::Silence ||
             to_level_ != Level<TLevel>::Silence;
    }

    // Mixes in into out.
    void Mix(TInSample* in,
             TOutSample* out,
             uint32_t in_channel_count,
             uint32_t out_channel_count,
             uint32_t frame_count,
             int64_t pts) {
      FTL_DCHECK(active()) << "Attempt to perform mix of silent channel.";

      // Align to correct channels.
      in += in_channel_;
      out += out_channel_;

      if (from_level_ != to_level_) {
        // The level varies. Need to fade.
        MixFade(in, out, in_channel_count, out_channel_count, frame_count, pts);
      } else if (from_level_ != Level<TLevel>::Unity) {
        // The level is constant but not unity. Need to multiply by the
        // constant.
        MixConstant(in, out, in_channel_count, out_channel_count, frame_count);
      } else {
        // The level is unity throughout. No need to multiply.
        MixUnity(in, out, in_channel_count, out_channel_count, frame_count);
      }
    }

    // Mixes in to out when from and to levels are both unity. in and out are
    // aligned to the correct channels.
    void MixUnity(TInSample* in,
                  TOutSample* out,
                  uint32_t in_channel_count,
                  uint32_t out_channel_count,
                  uint32_t frame_count);

    // Mixes in to out when from and to levels are the same value (not silence).
    // in and out are aligned to the correct channels.
    void MixConstant(TInSample* in,
                     TOutSample* out,
                     uint32_t in_channel_count,
                     uint32_t out_channel_count,
                     uint32_t frame_count);

    // Mixes in to out when from and to levels are the different. in and out
    // are aligned to the correct channels. pts indicates the PTS of the first
    // sample.
    void MixFade(TInSample* in,
                 TOutSample* out,
                 uint32_t in_channel_count,
                 uint32_t out_channel_count,
                 uint32_t frame_count,
                 int64_t pts);
  };

  // Gets the job for the given channels.
  Job& get_job(uint32_t in_channel, uint32_t out_channel) {
    FTL_DCHECK(in_channel < this->in_channel_count());
    FTL_DCHECK(out_channel < this->out_channel_count());
    // Job indexing needs to happen such that when iterating across all jobs
    // with an outer out_channel loop and an inner in_channel loop, simply
    // incrementing a Job pointer is adequate.
    return all_jobs_[in_channel + out_channel * this->in_channel_count()];
  }

  // Handles packet boundary transitions.
  void OnPacketTransition(int64_t pts);

  // Handles MixTable transitions.
  void OnMixdownScheduleTransition(int64_t pts);

  // Update the list of active jobs based on mixdown_from_ and mixdown_to_.
  void UpdateActiveJobs();

  // Returns the number of bytes in each input frame.
  uint32_t bytes_per_in_frame() {
    return this->in_channel_count() * sizeof(TInSample);
  }

  // Push job onto the front of the active_jobs_ list.
  void PushFrontJob(Job* job) {
    job->frontward_ = nullptr;
    job->backward_ = active_jobs_front_;
    if (active_jobs_front_ == nullptr) {
      FTL_DCHECK(active_jobs_back_ == nullptr);
      active_jobs_back_ = job;
    } else {
      FTL_DCHECK(active_jobs_front_->frontward_ == nullptr);
      active_jobs_front_->frontward_ = job;
    }
    active_jobs_front_ = job;
  }

  // Remove job from the active_jobs_ list.
  void EraseJob(Job* job) {
    if (active_jobs_front_ == job) {
      FTL_DCHECK(job->frontward_ == nullptr);
      active_jobs_front_ = job->backward_;
    } else {
      FTL_DCHECK(job->frontward_ != nullptr);
      job->frontward_->backward_ = job->backward_;
    }

    if (active_jobs_back_ == job) {
      FTL_DCHECK(job->backward_ == nullptr);
      active_jobs_back_ = job->frontward_;
    } else {
      FTL_DCHECK(job->backward_ != nullptr);
      job->backward_->frontward_ = job->frontward_;
    }
  }

  int64_t next_pts_;
  int64_t next_transition_pts_ = std::numeric_limits<int64_t>::max();
  int64_t next_packet_transition_pts_ = std::numeric_limits<int64_t>::max();
  int64_t next_mixdown_schedule_transition_pts_ =
      std::numeric_limits<int64_t>::max();
  bool end_of_stream_ = false;

  // We are always transitioning from mixdown_from_ to mixdown_to_, both of
  // which are always non-null. mixdown_to_ is a raw pointer obtained from
  // either mixdown_from_ (if mixdown_schedule_.top()->fade_ is false) or
  // mixdown_schedule_.top() (if mixdown_schedule_.top()->fade_ is true). We
  // need to make adjustments once the PTS reaches
  // mixdown_schedule_.top()->pts_.
  std::unique_ptr<MixdownScheduleEntry> mixdown_from_;
  const MixdownScheduleEntry* mixdown_to_;
  priority_queue_of_unique_ptr<MixdownScheduleEntry> mixdown_schedule_;

  std::deque<PacketPtr> packets_;
  PacketPtr current_packet_;
  TInSample* remaining_in_buffer_ = nullptr;

  std::unique_ptr<Job[]> all_jobs_;
  // Intrusive list of active jobs.
  Job* active_jobs_front_ = nullptr;
  Job* active_jobs_back_ = nullptr;
};

template <typename TInSample, typename TOutSample, typename TLevel>
MixerInputImpl<TInSample, TOutSample, TLevel>::MixerInputImpl(
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    int64_t first_pts)
    : MixerInput<TOutSample>(in_channel_count, out_channel_count, first_pts),
      next_pts_(first_pts),
      mixdown_from_(new MixdownScheduleEntry(
          MixdownTable<TLevel>::CreateSilent(in_channel_count,
                                             out_channel_count),
          std::numeric_limits<int64_t>::min(),
          false)),
      mixdown_to_(mixdown_from_.get()),
      all_jobs_(new Job[in_channel_count * out_channel_count]) {
  FTL_DCHECK(in_channel_count != 0);
  FTL_DCHECK(out_channel_count != 0);
  Job* job = &all_jobs_[0];
  for (uint32_t out_channel = 0; out_channel < this->out_channel_count();
       ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < this->in_channel_count();
         ++in_channel) {
      FTL_DCHECK(&get_job(in_channel, out_channel) == job);
      job->in_channel_ = in_channel;
      job->out_channel_ = out_channel;
      ++job;
    }
  }
}

template <typename TInSample, typename TOutSample, typename TLevel>
void MixerInputImpl<TInSample, TOutSample, TLevel>::AddMixdownScheduleEntry(
    std::unique_ptr<MixdownTable<TLevel>> mixdown_table,
    int64_t pts,
    bool fade) {
  FTL_DCHECK(mixdown_table);
  FTL_DCHECK(mixdown_table->in_channel_count() == this->in_channel_count());
  FTL_DCHECK(mixdown_table->out_channel_count() == this->out_channel_count());
  FTL_DCHECK(mixdown_from_);
  FTL_DCHECK(mixdown_to_);

  if (pts < mixdown_from_->pts_) {
    // The new entry would be older than the current 'from' entry. Ignore it.
    FTL_DLOG(WARNING) << "Mixdown schedule entry added at PTS " << pts
                      << " has already expired due to entry at PTS "
                      << mixdown_from_->pts_;
    return;
  }

  bool was_fading = mixdown_from_.get() != mixdown_to_;

  if (pts <= next_pts_) {
    // The new entry isn't older than the current 'from' entry but still in the
    // past. It replaces the current 'from' entry.
    mixdown_from_.reset(
        new MixdownScheduleEntry(std::move(mixdown_table), pts, fade));
    if (!was_fading) {
      // Not fading.
      mixdown_to_ = mixdown_from_.get();
    }

    // We've changed the 'from' entry, so the jobs will need to change.
    UpdateActiveJobs();
    return;
  }

  // Get a raw pointer to the table. We'll use this to determine whether the
  // table ended up at the top of the schedule.
  MixdownTable<TLevel>* mixdown_table_raw = mixdown_table.get();

  // Push the table into the schedule.
  mixdown_schedule_.push(std::make_unique<MixdownScheduleEntry>(
      MixdownScheduleEntry(std::move(mixdown_table), pts, fade)));

  // Determine if the new entry is at the top of the table, in which case we
  // need to do more work.
  if (mixdown_schedule_.get_top()->table_.get() != mixdown_table_raw) {
    // The new entry will be encountered later. Nothing more to do.
    return;
  }

  // We're replacing the top of the schedule, which is the next entry we'll
  // encounter. First, update the transition PTS.
  next_mixdown_schedule_transition_pts_ = pts;

  if (next_transition_pts_ > pts) {
    next_transition_pts_ = pts;
  }

  if (fade) {
    // We need to fade to the new entry.
    mixdown_to_ = mixdown_schedule_.get_top();
    UpdateActiveJobs();
    return;
  }

  // Not fading.
  mixdown_to_ = mixdown_from_.get();
  if (was_fading) {
    UpdateActiveJobs();
  }
}

template <typename TInSample, typename TOutSample, typename TLevel>
bool MixerInputImpl<TInSample, TOutSample, TLevel>::Mix(
    TOutSample* out_buffer,
    uint32_t out_frame_count,
    int64_t pts) {
  FTL_DCHECK(out_buffer);
  FTL_DCHECK(out_frame_count);
  FTL_DCHECK(pts < next_transition_pts_);  // TODO(dalesat): Relax this.
  FTL_DCHECK(next_transition_pts_ == next_packet_transition_pts_ ||
             next_transition_pts_ == next_mixdown_schedule_transition_pts_);

  // Loop until we've dealt with the entire output buffer. For every
  // iteration,
  // we'll update all three parameters to reflect progress.
  while (out_frame_count != 0) {
    // Figure out how far we can go. First, assume we can cover the remainder
    // of the output buffer.
    uint32_t frames_to_process = out_frame_count;

    // If the next transition occurs before the end of the output buffer, mix
    // to the transition.
    if (frames_to_process > next_transition_pts_ - pts) {
      frames_to_process = next_transition_pts_ - pts;
    }

    if (remaining_in_buffer_ == nullptr) {
      // There's no input for this region, so no need to mix.
    } else {
      // Run all the active mix jobs.
      for (Job* job = active_jobs_front_; job != nullptr;
           job = job->backward_) {
        job->Mix(remaining_in_buffer_, out_buffer, this->in_channel_count(),
                 this->out_channel_count(), frames_to_process, pts);
      }

      // Update the input buffer pointer to reflect progress.
      remaining_in_buffer_ += frames_to_process * this->in_channel_count();
    }

    // Update the parameters to reflect progress.
    out_buffer += frames_to_process * this->out_channel_count();
    out_frame_count -= frames_to_process;
    pts += frames_to_process;

    // Handle transitions.
    if (pts == next_transition_pts_) {
      if (pts == next_packet_transition_pts_) {
        OnPacketTransition(pts);
      }

      if (pts == next_mixdown_schedule_transition_pts_) {
        OnMixdownScheduleTransition(pts);
      }
    }
  }

  next_pts_ = pts;

  return !end_of_stream_;
}

template <typename TInSample, typename TOutSample, typename TLevel>
void MixerInputImpl<TInSample, TOutSample, TLevel>::OnPacketTransition(
    int64_t pts) {
  while (true) {
    if (current_packet_) {
      if (current_packet_->end_of_stream()) {
        end_of_stream_ = true;
      }

      current_packet_ = nullptr;
      remaining_in_buffer_ = nullptr;
    }

    if (packets_.empty()) {
      // Starving!
      FTL_DLOG(WARNING) << "Input starved at PTS " << pts << ".";
      next_packet_transition_pts_ = std::numeric_limits<int64_t>::max();
      break;
    }

    const PacketPtr& next_packet = packets_.front();
    if (next_packet->pts() > pts) {
      // Gap!
      FTL_DLOG(WARNING) << "Gap in input stream at PTS " << pts << ", "
                        << (next_packet->pts() - pts) << " frames missing.";
      next_packet_transition_pts_ = next_packet->pts();
      break;
    }

    current_packet_ = std::move(packets_.front());
    packets_.pop_front();

    remaining_in_buffer_ =
        reinterpret_cast<TInSample*>(current_packet_->payload());
    next_packet_transition_pts_ =
        current_packet_->pts() + current_packet_->size() / bytes_per_in_frame();

    if (current_packet_->pts() == pts) {
      break;
    }

    if (next_packet_transition_pts_ > pts) {
      // The front of this packet is too late.
      FTL_DLOG(WARNING) << "Packet with PTS " << current_packet_->pts()
                        << " arrived late, discarding "
                        << (next_packet_transition_pts_ - pts) << " frames.";
      remaining_in_buffer_ +=
          (next_packet_transition_pts_ - pts) * this->in_channel_count();
      break;
    }

    // The entire packet is too late. Eject it and try again.
    FTL_DLOG(WARNING) << "Packet with PTS " << current_packet_->pts()
                      << " arrived late, discarding.";
  }  // while(true)

  next_transition_pts_ = std::min(next_packet_transition_pts_,
                                  next_mixdown_schedule_transition_pts_);
}

template <typename TInSample, typename TOutSample, typename TLevel>
void MixerInputImpl<TInSample, TOutSample, TLevel>::OnMixdownScheduleTransition(
    int64_t pts) {
  FTL_DCHECK(mixdown_from_);
  FTL_DCHECK(mixdown_to_);
  FTL_DCHECK(!mixdown_schedule_.empty());

  mixdown_from_ = mixdown_schedule_.pop_and_move();

  if (mixdown_schedule_.empty()) {
    // Nothing to transition to. Assume we're doing this forever.
    mixdown_to_ = mixdown_from_.get();
    next_mixdown_schedule_transition_pts_ = std::numeric_limits<int64_t>::max();
  } else {
    const MixdownScheduleEntry* top = mixdown_schedule_.get_top();
    next_mixdown_schedule_transition_pts_ = top->pts_;
    if (top->fade_) {
      // Fade from mixdown_from_ to top.
      mixdown_to_ = top;
    } else {
      // Stay at mixdown_from_ until top->pts_.
      mixdown_to_ = mixdown_from_.get();
    }
  }

  UpdateActiveJobs();
  next_transition_pts_ = std::min(next_packet_transition_pts_,
                                  next_mixdown_schedule_transition_pts_);
}

template <typename TInSample, typename TOutSample, typename TLevel>
void MixerInputImpl<TInSample, TOutSample, TLevel>::UpdateActiveJobs() {
  FTL_DCHECK(mixdown_from_);
  FTL_DCHECK(mixdown_to_);

  Job* job = &all_jobs_[0];
  auto from_iter = mixdown_from_->table_->begin();
  auto to_iter = mixdown_to_->table_->begin();
  for (uint32_t out_channel = 0; out_channel < this->out_channel_count();
       ++out_channel) {
    for (uint32_t in_channel = 0; in_channel < this->in_channel_count();
         ++in_channel) {
      FTL_DCHECK(job->in_channel_ == in_channel);
      FTL_DCHECK(job->out_channel_ == out_channel);

      bool was_active = job->active();

      // Update the from_* values in the job. We only do this if we haven't
      // already arrived at the same from_* values through a fade. If we did
      // arrive at that value through a fade, there may be cumulative error
      // integrated into the job->from_level_ value, in which case we want to
      // correct that error over many samples to avoid a glitch.
      // TODO(dalesat): See if this is worth checking.
      if (!mixdown_from_->fade_ || job->to_level_ != *from_iter ||
          job->from_pts_ != mixdown_from_->pts_) {
        job->from_level_ = *from_iter;
        job->from_pts_ = mixdown_from_->pts_;
      }

      // Update the to_* values in the job.
      job->to_level_ = *to_iter;
      job->to_pts_ = mixdown_to_->pts_;

      if (job->active() != was_active) {
        if (job->active()) {
          PushFrontJob(job);
        } else {
          EraseJob(job);
        }
      }

      ++job;
      ++from_iter;
      ++to_iter;
    }
  }
}

// MixerInputImpl<float, float, float> template specializations.

template <>
void MixerInputImpl<float, float, float>::Job::MixUnity(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count);

template <>
void MixerInputImpl<float, float, float>::Job::MixConstant(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count);

template <>
void MixerInputImpl<float, float, float>::Job::MixFade(
    float* in,
    float* out,
    uint32_t in_channel_count,
    uint32_t out_channel_count,
    uint32_t frame_count,
    int64_t pts);

}  // namespace media
}  // namespace mojo
