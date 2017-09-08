// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <queue>

#pragma once

#include "lib/media/timeline/timeline_rate.h"
#include "lib/ftl/logging.h"

namespace media {

// A |Resampler| resamples frames producing frames of the same sample type and
// channel count. The sample type is given by |TSample|, and the channel count
// is passed to the constructor.
//
// Resampling is controlled using |TimelineRate| values where the 'reference'
// timeline corresponds to the output and the 'subject' timeline corresponds
// to the input. For example, with a rate of |TimelineRate(1, 1)|, a resampler
// produces one output frame for every input frame. With a rate of
// |TimelineRate(2, 1)|, a resampler produces one output frame for every two
// input frames. Likewise, with a rate of |TimelineRate(1, 2)|, a resampler
// produces two output frames for every input frame.
//
// Rates can change over time and are expressed to a resampler using the
// |AddRateScheduleEntry| call. The supplied rate applies when the PTS of the
// output reaches the specified PTS. More about PTS later. Any number of rate
// changes can be scheduled.
//
// The flow of resampler usage goes something like this:
//
//     resampler.SetPts(initial_pts);
//     resampler.AddRateScheduleEntry(rate0, pts0);
//     resampler.AddRateScheduleEntry(rate1, pts1);
//     // ...
//     while (not_done) {
//       if (resampler.need_in_frames()) {
//         GetNewInputBuffer(&in_buffer, &in_buffer_frame_count);
//         resampler.PutInFrames(in_buffer, in_buffer_frame_count);
//       }
//
//       if (resampler.need_out_frames()) {
//         GetNewOutputBuffer(&out_buffer, &out_buffer_frame_count);
//         resampler.PutOutFrames(out_buffer, out_buffer_frame_count);
//       }
//
//       resampler.Resample();
//     }
//
// |SetPts| allows the caller to set the PTS value corresponding to the next
// output frame to be produced by the resampler. This value is significant,
// because it's used to determine when scheduled rate transitions should occur.
// The PTS value is kept up-to-date as output frames are produced and can be
// obtained at any time by calling the |pts| getter.
//
// When consecutive input buffers are logically contiguous, a resampler may
// need to interpolate between the last frame of one buffer and the first frame
// of the next. For this reason, a resampler retains 'carry' state from one
// input buffer to the next. This information is only retained when an input
// buffer is completely exhausted and is only used when a new input buffer is
// 'put' using the |PutInFrames| method. \PutInFrames| has a |restart\ boolean
// parameter (which defaults to false), which tells the resampler to ignore
// carry state and start resampling at the start of the new buffer.

// Resamples audio frames.
// TODO(dalesat): Decay in_subframe_index_ when in_subframe_stride_ is 0.
// TODO(dalesat): Stereo specialization...subclass? template parameter?
// TODO(dalesat): More template specializations.
template <typename TSample, typename TSubframeIndex>
class Resampler {
 public:
  Resampler(uint32_t channel_count)
      : channel_count_(channel_count), in_sample_stride_(channel_count) {
    FTL_DCHECK(channel_count > 0);
    carry_frame_.resize(channel_count);
  }

  // Returns the number of samples per frame.
  uint32_t channel_count() { return channel_count_; }

  // Returns a pointer to the first unconsumed input frame in the input buffer.
  const TSample* in_frames() { return in_frames_; }

  // Returns the number of unconsumed input frames in the input buffer.
  size_t in_frames_remaining() {
    return (in_frames_end_ - in_frames_) / channel_count_;
  }

  // Indicates whether more input frames are needed.
  bool need_in_frames() { return in_frames_ == in_frames_end_; }

  // Returns a pointer to the first unfilled output frame in the output buffer.
  TSample* out_frames() { return out_frames_; }

  // Returns the number of unfilled output frames in the output buffer.
  size_t out_frames_remaining() {
    return (out_frames_end_ - out_frames_) / channel_count_;
  }

  // Indicates whether more output frames are needed.
  bool need_out_frames() { return out_frames_ == out_frames_end_; }

  // Gets the PTS of the next output frame to be produced.
  int64_t pts() { return pts_; }

  // Puts a new input buffer. |restart| indicates whether interpolation should
  // start fresh (true) or continue from the previous input buffer (false).
  void PutInFrames(const TSample* in_frames,
                   size_t in_frame_count,
                   bool restart = false) {
    in_frames_ = in_frames;
    in_frames_end_ = in_frames + (in_frame_count * channel_count_);

    if (restart) {
      one_based_in_frame_index_ = 1;
      in_subframe_index_ = 0;
    }
  }

  // Puts a new output buffer.
  void PutOutFrames(TSample* out_frames, size_t out_frame_count) {
    out_frames_ = out_frames;
    out_frames_end_ = out_frames + (out_frame_count * channel_count_);
  }

  // Sets the PTS of the next output frame to be produced.
  void SetPts(int64_t pts) { pts_ = pts; }

  // Schedule a rate change. In this context, the reference timeline refers to
  // the output of the resampler and the subject timeline refers to the input.
  // The rate transition is scheduled to occur at |pts|, which is a reference
  // (output) time.
  void AddRateScheduleEntry(TimelineRate rate, int64_t pts) {
    rate_schedule_.emplace(rate, pts);
  }

  // Resamples using the current input and output buffers, which must not be
  // exhausted. This method will always exhaust either the input buffer, the
  // output buffer or both.
  void Resample();

 private:
  static const TSubframeIndex kMaxSubframeIndex;

  struct RateScheduleEntry {
    TimelineRate rate_;
    int64_t pts_;

    RateScheduleEntry(TimelineRate rate, int64_t pts)
        : rate_(rate), pts_(pts) {}

    // This overload is defined in order to get the right entry ordering in
    // rate_table_. It doesn't distinguish entries that have the same PTS.
    bool operator<(const RateScheduleEntry& other) const {
      return pts_ < other.pts_;
    }
  };

  // Applies rate schedule entries up to the current PTS and updates
  // |out_frame_next_rate_change_|.
  void UpdateRate();

  // Resamples a constant-rate segment. This method updates
  // |one_based_in_frame_index_| and |in_subframe_index_| and copies the
  // last frame to |carry_frame_| as needed.
  void ResampleSegment();

  // Resamples a constant-rate segment assuming that interpolation is actually
  // required. This method is intended to be specialized for different values
  // of |TSample| and contains the performance-critical tight loop. This method
  // updates in_frames_, out_frames_, pts_ and in_subframe_index_.
  void InterpolateFrames();

  // Interpolates |channel_count_| samples from |in_a| and |in_b| to |out|. |t|
  // indicates the relative contributions of |in_a| and |in_b|. Given samples
  // a and b from |in_a| and |in_b|, respectively, the output sample is (a *
  // (kMaxSubframeIndex - t) + b * t) / kMaxSubframeIndex.
  void InterpolateFrame(const TSample* in_a,
                        const TSample* in_b,
                        TSubframeIndex t,
                        TSample* out);

  void CopyFrame(const TSample* in, TSample* out) {
    std::memcpy(out, in, sizeof(TSample) * channel_count_);
  }

  // Advances the input buffer by |steps| * |in_sample_stride_|.
  void AdvanceInput(size_t steps) {
    in_frames_ += steps * in_sample_stride_;
    FTL_DCHECK(in_frames_ <= in_frames_end_);
  }

  // Advances the output buffer by |frame_count| * |channel_count_| and |pts_|
  // by |frame_count|.
  void AdvanceOutput(size_t frame_count) {
    out_frames_ += channel_count_ * frame_count;
    pts_ += frame_count;
    FTL_DCHECK(out_frames_ <= out_frames_end_);
    FTL_DCHECK(out_frames_ <= out_frame_next_rate_change_);
  }

  // Advances the output buffer by |channel_count_| and |pts_| by 1.
  void IncrementOutput() {
    out_frames_ += channel_count_;
    pts_ += 1;
    FTL_DCHECK(out_frames_ <= out_frames_end_);
    FTL_DCHECK(out_frames_ <= out_frame_next_rate_change_);
  }

  TSubframeIndex SubframeStrideFromRate(TimelineRate rate);

  uint32_t channel_count_;
  std::priority_queue<RateScheduleEntry> rate_schedule_;

  // Points to the first remaining input frame.
  const TSample* in_frames_ = nullptr;

  // Points to the frame following the last frame in the input buffer.
  const TSample* in_frames_end_ = nullptr;

  // Points to the first remaining output frame.
  TSample* out_frames_ = nullptr;

  // Points to the frame following the last frame in the output buffer.
  TSample* out_frames_end_ = nullptr;

  // Points to the first sample in the output buffer to which a new rate
  // applies.
  TSample* out_frame_next_rate_change_ = nullptr;

  // The PTS of the next output frame that will be produced.
  int64_t pts_ = 0;

  // The integral part of the rate multiplied by |channel_count_|. This tells
  // us how much to increase the input frame pointer per output frame, ignoring
  // the fractional part of the rate.
  uint32_t in_sample_stride_;

  // The fractional part of the rate multiplied by |kTMax|. This tells us how
  // much to increment |in_subframe_index_| per output frame.
  // |in_subframe_index_| carries over into the input frame pointer when it
  // exceeds |kTMax|.
  TSubframeIndex in_subframe_stride_ = 0;

  // The current fractional part of the input frame index in the range 0 to
  // |kTMax|, exclusive of |kTMax|.
  TSubframeIndex in_subframe_index_ = 0;

  // The index of the next input frame to interpolate from, where a value of 1
  // indicates the frame referenced by |in_frames_|. A value of 0 indicates the
  // last frame of the previous input buffer, which is stashed in
  // |carry_frame_|.
  //
  // This field is generally set to 1. The exception occurs when an input buffer
  // is exhausted. In that case, this field may be set to 0, indicating the need
  // to interpolate from |carry_frame_| to the new |in_frames_|. It may also be
  // set to a value greater than 1, indicating that the first
  // |one_based_in_frame_index_| - 1 input frames from the new input buffer
  // should be skipped.
  uint32_t one_based_in_frame_index_ = 1;

  // |carry_frame_| is the last frame of the previous input buffer if
  // |one_based_in_frame_index_| is zero. Otherwise, its value is undefined.
  std::vector<TSample> carry_frame_;
};

template <typename TSample, typename TSubframeIndex>
void Resampler<TSample, TSubframeIndex>::Resample() {
  FTL_DCHECK(in_frames_ != nullptr);
  FTL_DCHECK(in_frames_ < in_frames_end_);
  FTL_DCHECK((in_frames_end_ - in_frames_) % channel_count_ == 0);
  FTL_DCHECK(out_frames_ != nullptr);
  FTL_DCHECK(out_frames_ < out_frames_end_);
  FTL_DCHECK((out_frames_end_ - out_frames_) % channel_count_ == 0);

  while (one_based_in_frame_index_ == 0) {
    // We need to interpolate from |carry_frame_|. We'll be producing output,
    // so we need to check for rate changes. There could be multiple rate
    // changes (unlikely), hence the outer loop.
    UpdateRate();

    while (one_based_in_frame_index_ == 0 &&
           out_frames_ < out_frame_next_rate_change_) {
      InterpolateFrame(carry_frame_.data(), in_frames_, in_subframe_index_,
                       out_frames_);

      in_subframe_index_ += in_subframe_stride_;
      if (in_subframe_index_ >= kMaxSubframeIndex) {
        in_subframe_index_ -= kMaxSubframeIndex;
        ++one_based_in_frame_index_;
      }

      one_based_in_frame_index_ += in_sample_stride_ / channel_count_;
      IncrementOutput();
    }

    // Note that if we're terminating this outer loop, we still need to skip
    // |one_based_in_frame_index_| - 1 input frames. The next chunk of code
    // does that anyway, so we just fall through.
  }

  if (one_based_in_frame_index_ > 1) {
    // We need to skip |one_based_in_frame_index_| - 1 input frames. This
    // doesn't entail producing any output frames, so we don't need to call
    // |UpdateRate|.
    size_t frames_to_skip =
        std::min(in_frames_remaining(),
                 static_cast<size_t>(one_based_in_frame_index_ - 1));

    in_frames_ += frames_to_skip;
    one_based_in_frame_index_ -= frames_to_skip;
  }

  while (!need_in_frames() && !need_out_frames()) {
    UpdateRate();
    ResampleSegment();
  }
}

template <typename TSample, typename TSubframeIndex>
void Resampler<TSample, TSubframeIndex>::UpdateRate() {
  while (!rate_schedule_.empty() && rate_schedule_.top().pts_ <= pts_) {
    TimelineRate rate = rate_schedule_.top().rate_;
    rate_schedule_.pop();

    in_sample_stride_ =
        channel_count_ * (rate.subject_delta() / rate.reference_delta());

    in_subframe_stride_ = SubframeStrideFromRate(rate);

    FTL_DCHECK(in_subframe_stride_ < kMaxSubframeIndex);
  }

  if (rate_schedule_.empty()) {
    out_frame_next_rate_change_ = out_frames_end_;
    return;
  }

  size_t until_rate_change = rate_schedule_.top().pts_ - pts_;

  if (until_rate_change > out_frames_remaining()) {
    out_frame_next_rate_change_ = out_frames_end_;
    return;
  }

  out_frame_next_rate_change_ =
      out_frames_ + until_rate_change * channel_count_;
}

template <typename TSample, typename TSubframeIndex>
void Resampler<TSample, TSubframeIndex>::ResampleSegment() {
  FTL_DCHECK(in_frames_ != nullptr);
  FTL_DCHECK(in_frames_ < in_frames_end_);
  FTL_DCHECK((in_frames_end_ - in_frames_) % channel_count_ == 0);
  FTL_DCHECK(out_frames_ != nullptr);
  FTL_DCHECK(out_frames_ < out_frames_end_);
  FTL_DCHECK((out_frames_end_ - out_frames_) % channel_count_ == 0);
  FTL_DCHECK(out_frames_ < out_frame_next_rate_change_);
  FTL_DCHECK(out_frame_next_rate_change_ <= out_frames_end_);
  FTL_DCHECK((out_frame_next_rate_change_ - out_frames_) % channel_count_ == 0);

  if (in_subframe_index_ == 0 && in_subframe_stride_ == 0) {
    // We don't need to interpolate.
    size_t frames_to_copy = static_cast<size_t>(
        (out_frame_next_rate_change_ - out_frames_) / channel_count_);

    if (in_sample_stride_ == channel_count_) {
      // Just need to copy.
      frames_to_copy = std::min(in_frames_remaining(), frames_to_copy);
      std::memcpy(out_frames_, in_frames_,
                  sizeof(TSample) * channel_count_ * frames_to_copy);
    } else {
      // Just need to copy individual frames.
      frames_to_copy = std::min(
          (in_frames_remaining() + in_sample_stride_ - 1) / in_sample_stride_,
          frames_to_copy);

      const TSample* in = in_frames_;
      TSample* out = out_frames_;
      for (size_t i = frames_to_copy; i > 0; --i) {
        CopyFrame(in, out);
        in += in_sample_stride_;
        out += channel_count_;
      }
    }

    AdvanceInput(frames_to_copy);
    AdvanceOutput(frames_to_copy);
  } else {
    InterpolateFrames();
  }

  // |InterpolateFrames| stops when in_frames_ reaches or exceeds the last
  // frame in the input buffer. We need to do some cleanup here.

  if (in_frames_ >= in_frames_end_) {
    // We've skipped into the new buffer. Set |one_based_in_frame_index_|
    // properly and set |in_frames_| so we know we've run out of input frames.
    one_based_in_frame_index_ =
        (in_frames_ - in_frames_end_) / channel_count_ + 1;
    in_frames_ = in_frames_end_;
  } else if (in_frames_ + channel_count_ == in_frames_end_) {
    // We've advanced to the last input frame.
    if (!need_out_frames() && in_subframe_index_ == 0) {
      // We have room for an output frame, and |InterpolateFrames| has left us
      // precisely on the last input frame. We can just copy that last frame
      // and set things up for the next input buffer.
      CopyFrame(in_frames_, out_frames_);
      IncrementOutput();
      one_based_in_frame_index_ = in_sample_stride_ / channel_count_;
      in_subframe_index_ = in_subframe_stride_;
    } else {
      // We need to interpolate between that last frame of this input buffer and
      // the first frame of the next, so set |one_based_in_frame_index_|
      // accordingly.
      one_based_in_frame_index_ = 0;
    }

    // When |one_based_in_frame_index_| is 0, the next output frame will be
    // interpolated between the last frame of this input buffer and the first
    // frame of the next input buffer. We need to save the last frame so we can
    // do this interpolation next time.
    if (one_based_in_frame_index_ == 0) {
      CopyFrame(in_frames_, carry_frame_.data());
    }

    // We've exhausted the input buffer.
    in_frames_ = in_frames_end_;
  }
}

template <typename TSample, typename TSubframeIndex>
TSubframeIndex Resampler<TSample, TSubframeIndex>::SubframeStrideFromRate(
    TimelineRate rate) {
  return (static_cast<TSubframeIndex>(rate.subject_delta() %
                                      rate.reference_delta()) *
          kMaxSubframeIndex) /
         rate.reference_delta();
}

// Resampler<float, float> template specializations

template <>
// static
const float Resampler<float, float>::kMaxSubframeIndex;

template <>
void Resampler<float, float>::InterpolateFrame(const float* in_a,
                                               const float* in_b,
                                               float t,
                                               float* out);

template <>
void Resampler<float, float>::InterpolateFrames();

// Resampler<int16_t, uint16_t> template specializations

template <>
// static
const uint16_t Resampler<int16_t, uint16_t>::kMaxSubframeIndex;

template <>
void Resampler<int16_t, uint16_t>::InterpolateFrame(const int16_t* in_a,
                                                    const int16_t* in_b,
                                                    uint16_t t,
                                                    int16_t* out);

template <>
void Resampler<int16_t, uint16_t>::InterpolateFrames();

template <>
uint16_t Resampler<int16_t, uint16_t>::SubframeStrideFromRate(
    TimelineRate rate);

}  // namespace media
