// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/reusable_buffer.h"

#include <ffl/string.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"

namespace media_audio {

ReusableBuffer::ReusableBuffer(Format format, int64_t capacity_frames)
    : capacity_frames_(capacity_frames), format_(format) {
  FX_CHECK(capacity_frames > 0);
  buf_.reserve(format_.bytes_per_frame() * capacity_frames);
}

void ReusableBuffer::Reset(Fixed start_frame) {
  FX_CHECK(start_frame.Fraction() == Fixed(0))
      << ffl::String::DecRational << "buffer cannot have fractional position " << start_frame;

  start_frame_ = start_frame;
  buf_.clear();
}

void ReusableBuffer::Append(Fixed new_payload_start, int64_t new_payload_frames, void* new_payload,
                            const char* caller) {
  FX_CHECK(start_frame_.has_value()) << caller << ": cannot append without first calling Reset";

  FX_CHECK(new_payload_start.Fraction() == Fixed(0))
      << ffl::String::DecRational << caller << ": cannot append to fractional position "
      << new_payload_start;

  FX_CHECK(new_payload_start >= end_frame())
      << ffl::String::DecRational << caller << ": cannot append to " << new_payload_start
      << " from [" << start_frame() << ", " << end_frame() << ")";

  // Length of a silent gap, if any.
  int64_t gap = Fixed(new_payload_start - end_frame()).Floor();
  FX_CHECK(gap >= 0);  // verified above

  FX_CHECK(frame_count() + gap + new_payload_frames <= capacity())
      << "cannot append " << new_payload_frames << " frames after gap of " << gap << " frames to "
      << frame_count() << "frames, would exceed maximum capacity of " << capacity() << " frames";

  // Insert a silent gap if needed.
  if (gap > 0) {
    PushSilence(gap);
  }

  if (new_payload) {
    char* source = reinterpret_cast<char*>(new_payload);
    buf_.insert(buf_.end(), source, source + new_payload_frames * format_.bytes_per_frame());
  } else {
    PushSilence(new_payload_frames);
  }
}

void ReusableBuffer::PushSilence(int64_t frames) {
  buf_.resize(buf_.size() + frames * format_.bytes_per_frame(), 0);
}

}  // namespace media_audio
