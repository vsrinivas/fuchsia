// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/ring_buffer.h"

#include <lib/syslog/cpp/macros.h>

#include <cstring>
#include <string>
#include <vector>

#include "src/developer/forensics/feedback_data/system_log_recorder/encoding/lz4_utils.h"

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

RingBuffer::RingBuffer(const size_t buffer_size)
    : ring_buffer_(buffer_size), write_size_(kMaxChunkSize) {
  ring_ptr_ = ring_buffer_.data();
  ring_start_ = ring_buffer_.data();
  ring_end_ = ring_start_ + ring_buffer_.size();
}

void RingBuffer::Advance(const size_t pos) {
  ring_ptr_ += pos;
  // Wrap the data index around if there is a possibility that the data index falls outside the ring
  // buffer.
  if (ring_ptr_ + write_size_ >= ring_end_) {
    ring_ptr_ = ring_start_;
  }
}

char* RingBuffer::Write(const char* chunk, const size_t chunk_size) {
  FX_CHECK(chunk_size <= write_size_);
  char* ptr = ring_ptr_;
  std::memcpy(ring_ptr_, chunk, chunk_size);
  Advance(chunk_size);
  return ptr;
}

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics
