// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_REUSABLE_BUFFER_H_
#define SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_REUSABLE_BUFFER_H_

#include <lib/syslog/cpp/macros.h>

#include <memory>
#include <utility>
#include <vector>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/common/logging.h"

namespace media_audio {

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
  ReusableBuffer(Format format, int64_t capacity_frames);

  // No copying or moving.
  ReusableBuffer(const ReusableBuffer&) = delete;
  ReusableBuffer& operator=(const ReusableBuffer&) = delete;
  ReusableBuffer(ReusableBuffer&&) = delete;
  ReusableBuffer& operator=(ReusableBuffer&&) = delete;

  // Reports the starting frame of this buffer.
  // REQUIRED: the buffer has been reset.
  Fixed start_frame() const {
    FX_CHECK(start_frame_.has_value());
    return *start_frame_;
  }

  // Reports the end of the buffer. Like std::vector::end(), this is one frame past the last frame.
  // REQUIRED: the buffer has been reset.
  Fixed end_frame() const { return start_frame() + Fixed(frame_count()); }

  // Reports the total number of frames appended to the buffer since the last `Reset`.
  int64_t frame_count() const {
    return static_cast<int64_t>(buf_.size()) / format_.bytes_per_frame();
  }

  // Reports whether the buffer is empty.
  bool empty() const { return buf_.empty(); }

  // Reports the maximum capacity of this buffer in frames.
  int64_t capacity() const { return capacity_frames_; }

  // Returns a pointer to the raw data.
  // It is undefined behavior to access the payload beyond `length()` frames.
  //
  // REQUIRED: the buffer is not empty.
  void* payload() {
    // This is required because of a technicality: although std::vector::reserve() allocates
    // storage for the vector, index operations like `buf_[x]` technically have undefined behavior
    // when `x > buf_.size()`, despite this preallocation.
    FX_CHECK(!empty());
    return buf_.data();
  }

  // Reports the payload's format.
  const Format& format() const { return format_; }

  // Clears the buffer and resets the starting position.
  // This must be called at least once after the constructor before appending any data.
  //
  // REQUIRED: start_frame.Fraction() == 0
  void Reset(Fixed start_frame);

  // Appends the given payload buffer.
  // If `payload_start > end()`, silence is automatically inserted in the gap.
  //
  // REQUIRED: payload_start.Fraction() == 0 &&
  //           payload_start >= end() &&
  //           does not overflow capacity &&
  //           the buffer has been reset
  void AppendData(Fixed payload_start, int64_t payload_frames, void* payload) {
    FX_CHECK(payload);
    Append(payload_start, payload_frames, payload, "AppendData");
  }

  // Appends silent frames.
  //
  // REQUIRED: silence_start.Fraction() == 0 &&
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

  std::optional<Fixed> start_frame_;  // first frame in this buffer, or nullopt if not `Reset`
  std::vector<char> buf_;
};

}  // namespace media_audio

#endif  // SRC_MEDIA_AUDIO_SERVICES_MIXER_MIX_REUSABLE_BUFFER_H_
