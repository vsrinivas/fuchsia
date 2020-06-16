// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_

#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::mixer {

// ChannelStrip lightly manages sections of single-channel audio, useful when processing audio one
// channel at a time. ChannelStrip is essentially a vector-of-vectors, but also contains convenience
// methods to shift audio (either all-channels or a single channel) within each channel's "strip".
class ChannelStrip {
 public:
  ChannelStrip(uint32_t num_channels, uint32_t length)
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
  static inline void Display(const ChannelStrip& channels);

  // Zero out all channels, leaving each strip (vector) at the specified length
  void Clear() {
    for (auto& channel : data_) {
      std::memset(channel.data(), 0, channel.size() * sizeof(channel[0]));
    }
  }

  // Shift the audio in all channels, by the specified amount
  void ShiftBy(size_t shift_by) {
    shift_by = std::min(shift_by, len_);

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

  uint32_t num_channels() const { return num_channels_; }
  size_t length() const { return len_; }

 private:
  std::vector<std::vector<float>> data_;
  uint32_t num_channels_;
  size_t len_;
};

// declared static, used for debugging purposes only
// Log the contents of the channel strip, channel by channel.
inline void ChannelStrip::Display(const ChannelStrip& channels) {
  FX_LOGS(TRACE) << "ChannelStrip: chans " << channels.num_channels_ << ", len 0x" << std::hex
                 << channels.len_;

  for (auto chan = 0u; chan < channels.num_channels_; ++chan) {
    FX_LOGS(TRACE) << "           channel " << chan;
    char str[256];
    str[0] = 0;
    int n = 0;
    for (auto idx = 0u; idx < channels.len_; ++idx) {
      if (idx % 16 == 0) {
        FX_LOGS(TRACE) << str;
        n = sprintf(str, "[%4x]  ", idx);
      }
      n += sprintf(str + n, "%6.03f ", channels.data_[chan][idx]);
    }
    FX_LOGS(TRACE) << str;
  }
}

}  // namespace media::audio::mixer

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_
