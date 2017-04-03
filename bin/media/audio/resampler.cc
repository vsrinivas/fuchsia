// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/resampler.h"

namespace media {
namespace {

constexpr int16_t kMaxSubframeBits_uint16_t = 14;

}  // namespace

// Resampler<float, float> template specializations

template <>
// static
const float Resampler<float, float>::kMaxSubframeIndex = 1.0f;

template <>
void Resampler<float, float>::InterpolateFrame(const float* in_a,
                                               const float* in_b,
                                               float in_subframe_index,
                                               float* out) {
  for (uint32_t i = 0; i < channel_count_; ++i) {
    *out = (*in_a * (kMaxSubframeIndex - in_subframe_index) +
            *in_b * in_subframe_index) /
           kMaxSubframeIndex;
    ++in_a;
    ++in_b;
    ++out;
  }
}

template <>
void Resampler<float, float>::InterpolateFrames() {
  const float* in = in_frames_;
  const float* in_last = in_frames_end_ - channel_count_;
  float* out = out_frames_;
  float* out_end = out_frames_end_;
  float in_subframe_index = in_subframe_index_;
  float in_subframe_stride = in_subframe_stride_;
  uint32_t in_stride = in_sample_stride_;
  uint32_t channel_count = channel_count_;

  while (in < in_last && out < out_end) {
    if (in_subframe_index == 0) {
      CopyFrame(in, out);
    } else {
      InterpolateFrame(in, in + channel_count_, in_subframe_index, out);
    }

    in_subframe_index += in_subframe_stride;
    if (in_subframe_index >= kMaxSubframeIndex) {
      in_subframe_index -= kMaxSubframeIndex;
      in += channel_count;
    }

    in += in_stride;
    out += channel_count;
  }

  pts_ += (out - out_frames_) / channel_count;

  in_frames_ = in;
  out_frames_ = out;
  in_subframe_index_ = in_subframe_index;
}

// Resampler<int16_t, uint16_t> template specializations

template <>
// static
const uint16_t Resampler<int16_t, uint16_t>::kMaxSubframeIndex =
    1 << kMaxSubframeBits_uint16_t;

template <>
void Resampler<int16_t, uint16_t>::InterpolateFrame(const int16_t* in_a,
                                                    const int16_t* in_b,
                                                    uint16_t in_subframe_index,
                                                    int16_t* out) {
  for (uint32_t i = 0; i < channel_count_; ++i) {
    *out = static_cast<int16_t>(
        (static_cast<int32_t>(*in_a) * (kMaxSubframeIndex - in_subframe_index) +
         static_cast<int32_t>(*in_b) * in_subframe_index) >>
        kMaxSubframeBits_uint16_t);
    ++in_a;
    ++in_b;
    ++out;
  }
}

template <>
void Resampler<int16_t, uint16_t>::InterpolateFrames() {
  const int16_t* in = in_frames_;
  const int16_t* in_last = in_frames_end_ - channel_count_;
  int16_t* out = out_frames_;
  int16_t* out_end = out_frames_end_;
  uint16_t in_subframe_index = in_subframe_index_;
  uint16_t in_subframe_stride = in_subframe_stride_;
  uint32_t in_stride = in_sample_stride_;
  uint32_t channel_count = channel_count_;

  while (in < in_last && out < out_end) {
    if (in_subframe_index == 0) {
      CopyFrame(in, out);
    } else {
      InterpolateFrame(in, in + channel_count_, in_subframe_index, out);
    }

    in_subframe_index += in_subframe_stride;
    if (in_subframe_index >= kMaxSubframeIndex) {
      in_subframe_index -= kMaxSubframeIndex;
      in += channel_count;
    }

    in += in_stride;
    out += channel_count;
  }

  pts_ += (out - out_frames_) / channel_count;

  in_frames_ = in;
  out_frames_ = out;
  in_subframe_index_ = in_subframe_index;
}

template <>
uint16_t Resampler<int16_t, uint16_t>::SubframeStrideFromRate(
    TimelineRate rate) {
  return (static_cast<int16_t>(rate.subject_delta() % rate.reference_delta())
          << kMaxSubframeBits_uint16_t) /
         rate.reference_delta();
}

}  // namespace media
