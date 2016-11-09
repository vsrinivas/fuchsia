// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace media {

// Mixes a stream of packets into output buffers. This abstract base class
// exists so that Mixer can accept inputs with a variety of input sample types
// and level types.
template <typename TOutSample>
class MixerInput {
 protected:
  MixerInput(uint32_t in_channel_count,
             uint32_t out_channel_count,
             int64_t first_pts)
      : in_channel_count_(in_channel_count),
        out_channel_count_(out_channel_count),
        first_pts_(first_pts) {}

 public:
  uint32_t in_channel_count() const { return in_channel_count_; }

  uint32_t out_channel_count() const { return out_channel_count_; }

  // Returns the first PTS for which this input has samples to contribute.
  int64_t first_pts() const { return first_pts_; }

  bool operator<(const MixerInput& other) {
    return first_pts() < other.first_pts();
  }

  // Mixes the input into the output buffer. Returns true if the input is
  // still relevant after this mix, false if not.
  virtual bool Mix(TOutSample* out_buffer,
                   uint32_t out_frame_count,
                   int64_t pts) = 0;

 private:
  uint32_t in_channel_count_;
  uint32_t out_channel_count_;
  int64_t first_pts_;
};

}  // namespace media
