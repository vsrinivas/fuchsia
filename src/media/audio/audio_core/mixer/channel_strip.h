// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace media::audio::mixer {

// ChannelStrip lightly manages sections of single-channel audio, useful when processing audio one
// channel at a time. ChannelStrip is essentially a vector-of-vectors, but also contains convenience
// methods to shift audio (either all-channels or a single channel) within each channel's "strip".
class ChannelStrip {
 public:
  ChannelStrip(int32_t num_channels, int32_t length)
      : data_(num_channels), num_channels_(num_channels), len_(length) {
    FX_DCHECK(num_channels > 0);
    FX_DCHECK(length > 0);

    for (auto& channel : data_) {
      channel.resize(len_, 0.0f);
    }
  }

  ChannelStrip() : ChannelStrip(1, 1) {}
  ChannelStrip(const ChannelStrip& not_ctor_copyable) = delete;
  ChannelStrip& operator=(const ChannelStrip& not_copyable) = delete;
  ChannelStrip(ChannelStrip&& not_ctor_movable) = delete;
  ChannelStrip& operator=(ChannelStrip&& not_movable) = delete;

  // Used for debugging purposes only
  static inline std::string ToString(const ChannelStrip& channels);

  // Zero out all channels, leaving each strip (vector) at the specified length
  void Clear() {
    for (auto& channel : data_) {
      std::memset(channel.data(), 0, channel.size() * sizeof(channel[0]));
    }
  }

  // Shift the audio in all channels, by the specified amount
  void ShiftBy(size_t shift_by) {
    shift_by = std::min(shift_by, static_cast<size_t>(len_));

    for (auto& channel : data_) {
      memmove(channel.data(), channel.data() + shift_by, (len_ - shift_by) * sizeof(channel[0]));
      memset(channel.data() + (len_ - shift_by), 0, shift_by * sizeof(channel[0]));
    }
  }

  // This returns a vector containing audio data for a single channel. ChannelStrip is essentially a
  // vector-of-vector and is not "jagged": all channels have the same amount of audio data (all
  // vectors are the same length).
  // For this reason, one MUST NOT perform the following to a channel vector returned by []operator:
  //  - change the underlying vector: ctor, dtor, operator=, assign, swap
  //  - change the vector's size: clear, emplace[_back], erase[_if], insert, pop, push, resize,
  //    or operator[] to set a value at index len_ or greater
  // Only the following can be performed on the vector returned by []operator:
  //  - read/write existing audio samples: operator[] < len, at, back, data, front, iterators
  //  - query size/capacity (allowed but unneeded): capacity, empty, max_size, reserve, size, ...
  std::vector<float>& operator[](size_t index) { return data_[index]; }

  int32_t num_channels() const { return num_channels_; }
  int64_t length() const { return len_; }

 private:
  std::vector<std::vector<float>> data_;
  int32_t num_channels_;
  int64_t len_;
};

// declared static, used for debugging purposes only
// Log the contents of the channel strip, channel by channel.
inline std::string ChannelStrip::ToString(const ChannelStrip& channels) {
  std::string strip("ChannelStrip: chans ");
  strip += std::to_string(channels.num_channels_) + ", len " + std::to_string(channels.len_) + "\n";

  for (auto chan = 0; chan < channels.num_channels_; ++chan) {
    strip += "\tChannel " + std::to_string(chan);

    for (auto idx = 0; idx < channels.len_; ++idx) {
      if (idx % 16 == 0) {
        strip += "\n[ " + std::to_string(idx) + "\t]";
      }
      strip += "\t" + std::to_string(channels.data_[chan][idx]);
    }
    strip += "\n";
  }
  return strip;
}

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_
