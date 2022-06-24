// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_LIB_PROCESSING_CHANNEL_STRIP_H_
#define SRC_MEDIA_AUDIO_LIB_PROCESSING_CHANNEL_STRIP_H_

#include <lib/stdcompat/span.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <cstring>
#include <sstream>
#include <string>

namespace media_audio {

// Class that manages planar audio data. This is useful when processing audio one channel at a time.
class ChannelStrip {
 public:
  ChannelStrip(int64_t channel_count, int64_t frame_count)
      : data_(new float[channel_count * frame_count]{}),
        channel_count_(channel_count),
        frame_count_(frame_count) {
    FX_DCHECK(channel_count > 0);
    FX_DCHECK(frame_count > 0);
  }
  ~ChannelStrip() { delete[] data_; }

  // Non-copyable and non-movable.
  ChannelStrip(const ChannelStrip& other) = delete;
  ChannelStrip& operator=(const ChannelStrip& other) = delete;
  ChannelStrip(ChannelStrip&& other) = delete;
  ChannelStrip& operator=(ChannelStrip&& other) = delete;

  // Logs the contents of the channel strip, channel by channel.
  // Used for debugging purposes only.
  static std::string ToString(const ChannelStrip& channel_strip) {
    std::stringstream ss;
    ss << "ChannelStrip: chans ";
    ss << channel_strip.channel_count_ << ", len " << channel_strip.frame_count_ << "\n";
    for (auto chan = 0; chan < channel_strip.channel_count_; ++chan) {
      ss << "\tChannel " << chan;
      for (auto i = 0; i < channel_strip.frame_count_; ++i) {
        if (i % 16 == 0) {
          ss << "\n[ " << i << "\t]";
        }
        ss << "\t" << channel_strip.data_[chan * channel_strip.frame_count_ + i];
      }
      ss << "\n";
    }
    return ss.str();
  }

  // Zeroes out all channels.
  void Clear() { std::fill_n(data_, channel_count_ * frame_count_, 0.0f); }

  // Shifts the audio data in all channels, by `shift_by` amount.
  void ShiftBy(size_t shift_by) {
    shift_by = std::min(shift_by, static_cast<size_t>(frame_count_));
    for (auto chan = 0; chan < channel_count_; ++chan) {
      float* channel_data = data_ + chan * frame_count_;
      std::memmove(channel_data, channel_data + shift_by,
                   (frame_count_ - shift_by) * sizeof(data_[0]));
      std::fill_n(channel_data + (frame_count_ - shift_by), shift_by, 0.0f);
    }
  }

  // Returns a span containing audio data for a given `channel`.
  cpp20::span<float> operator[](size_t channel) {
    return cpp20::span{data_ + channel * frame_count_, static_cast<size_t>(frame_count_)};
  }
  cpp20::span<const float> operator[](size_t channel) const {
    return cpp20::span{data_ + channel * frame_count_, static_cast<size_t>(frame_count_)};
  }

  // Returns the number of channels.
  int64_t channel_count() const { return channel_count_; }

  // Returns the number of frames (i.e., length of each channel).
  int64_t frame_count() const { return frame_count_; }

 private:
  float* data_;
  int64_t channel_count_;
  int64_t frame_count_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_CHANNEL_STRIP_H_
