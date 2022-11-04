// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CHANNEL_ATTRIBUTES_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CHANNEL_ATTRIBUTES_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <vector>

namespace media::audio {

struct ChannelAttributes {
  // If we make the following 2 assumptions, then we need only specify one boundary frequency:
  // 1) if any channel in a channel set touches the audible range, then the channel set will cover
  // enough of the audible range to be a useful output;
  // 2) if a channel touches the ultrasonic range, then it will cover the entire range needed for
  // current content; it can (if needed) be the sole channel that emits ultrasonic frequencies.
  static constexpr uint32_t kAudibleUltrasonicBoundaryHz = 24000;

  ChannelAttributes() = default;
  ChannelAttributes(uint32_t min_freq, uint32_t max_freq)
      : min_frequency(min_freq), max_frequency(max_freq) {
    FX_DCHECK(min_frequency <= max_frequency);
  }

  // Return true if this channel covers ANY portion of the audible range.
  // Must include more than just the boundary values.
  bool IncludesAudible() const {
    return (min_frequency < kAudibleUltrasonicBoundaryHz && max_frequency > 0);
  }

  // Return true if this channel covers ANY portion of the ultrasonic range
  // Must include more than just the boundary value.
  bool IncludesUltrasonic() const {
    return (max_frequency > kAudibleUltrasonicBoundaryHz &&
            min_frequency < fuchsia::media::MAX_PCM_FRAMES_PER_SECOND / 2);
  }

  // Static methods that operate over a vector of ChannelAttributes, representing a channel set
  //
  // Supporting audible requires a single channel to support ANY non-empty frequency range
  // within these bounds (it need not cover the ENTIRE range).
  static bool IncludesAudible(std::vector<ChannelAttributes>& channels) {
    return std::any_of(channels.cbegin(), channels.cend(),
                       [](const auto& channel) { return channel.IncludesAudible(); });
  }

  // Supporting ultrasonic requires a channel to support ANY non-empty frequency range within these
  // bounds (it need not cover the ENTIRE range).
  // This simplifying assumption is valid for currently-known audio devices, because if they touch
  // the ultrasonic range, they cover the entire frequency range of commonly-used ultrasonic
  // content. Ultimately, we will need the device to cover this range (not just a single frequency)
  // for ultrasonic content to be effective.
  static bool IncludesUltrasonic(std::vector<ChannelAttributes>& channels) {
    return std::any_of(channels.cbegin(), channels.cend(),
                       [](const auto& channel) { return channel.IncludesUltrasonic(); });
  }

  uint32_t min_frequency;
  uint32_t max_frequency;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_CHANNEL_ATTRIBUTES_H_
