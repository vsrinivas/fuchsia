// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REUSABLE_BUFFER_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REUSABLE_BUFFER_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/media/audio/audio_core/v1/mixer/output_producer.h"
#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/format/format.h"

namespace media::audio {

// This class contains an audio buffer along with a frame number that identifies the
// first frame in the buffer:
//
//     +-----------------------------------+
//     |             buffer                |
//     +-----------------------------------+
//     ^                                   ^
//     start frame                         end frame
//
// The buffer is initially empty. Audio data can be appended up to a specified capacity.
// The buffer can be cleared for reuse. The capacity is preallocated by the constructor,
// after which there are no further allocations.
//
// All frames must be aligned on integral positions. Despite this integral requirement,
// method calls represent frame positions with Fixed numbers for consistency with other
// classes in this directory.
class ReusableBuffer {
 public:
  ReusableBuffer(const Format& format, int64_t capacity_frames);

  // No copying or moving.
  ReusableBuffer(const ReusableBuffer&) = delete;
  ReusableBuffer& operator=(const ReusableBuffer&) = delete;
  ReusableBuffer(ReusableBuffer&&) = delete;
  ReusableBuffer& operator=(ReusableBuffer&&) = delete;

  // Reports the starting frame of this buffer.
  // REQUIRES: the buffer has been reset.
  Fixed start() const {
    FX_CHECK(start_.has_value());
    return *start_;
  }

  // Reports the end of this buffer. Like std::vector::end(), this is one frame past the last frame.
  // REQUIRES: the buffer has been reset.
  Fixed end() const { return start() + Fixed(length()); }

  // Reports the total number of frames appended to the buffer since the last `Reset()`.
  int64_t length() const { return static_cast<int64_t>(buf_.size()) / format_.bytes_per_frame(); }

  // Reports whether the buffer is empty.
  bool empty() const { return buf_.empty(); }

  // Reports the maximum capacity of this buffer, in frames.
  int64_t capacity() const { return capacity_frames_; }

  // Returns a pointer to the raw data.
  // It is undefined behavior to access the payload beyond `length()` frames.
  //
  // REQUIRES: the buffer is not empty.
  void* payload() {
    // This is required because of a technicality: although std::vector::reserve() allocates
    // storage for the vector, index operations like buf_[x] technically have undefined behavior
    // when x > buf_.size(), despite this preallocation.
    FX_CHECK(!empty());
    return &buf_[0];
  }

  // Reports the payload's format.
  const Format& format() const { return format_; }

  // Clears the buffer and resets the starting position.
  // This must be called at least once after the constructor before appending any data.
  //
  // REQUIRES: start_frame.Fraction() == 0
  void Reset(Fixed start_frame);

  // Appends the given payload buffer.
  // If `payload_start > end()`, silence is automatically inserted in the gap.
  //
  // REQUIRES: payload_start.Fraction() == 0 &&
  //           payload_start >= end() &&
  //           does not overflow capacity &&
  //           the buffer has been reset
  void AppendData(Fixed payload_start, int64_t payload_frames, void* payload) {
    FX_CHECK(payload);
    Append(payload_start, payload_frames, payload, "AppendData");
  }

  // Appends silent frames.
  //
  // REQUIRES: silence_start.Fraction() == 0 &&
  //           silence_start >= end() &&
  //           does not overflow capacity &&
  //           the buffer has been reset
  void AppendSilence(Fixed silence_start, int64_t silence_frames) {
    Append(silence_start, silence_frames, nullptr, "AppendSilence");
  }

 private:
  void Append(Fixed payload_start, int64_t payload_frames, void* payload, const char* caller);
  void PushSilence(int64_t frames);

  const int64_t capacity_frames_;
  const Format format_;
  const std::unique_ptr<OutputProducer> output_producer_;

  std::optional<Fixed> start_;  // first frame in this buffer, or nullopt if not Reset
  std::vector<char> buf_;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_REUSABLE_BUFFER_H_
