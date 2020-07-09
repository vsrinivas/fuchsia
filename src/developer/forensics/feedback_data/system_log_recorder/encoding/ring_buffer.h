// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_RING_BUFFER_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_RING_BUFFER_H_

#include <cstring>
#include <string>
#include <vector>

namespace forensics {
namespace feedback_data {
namespace system_log_recorder {

// Keeps raw data in memory as long as it is required by the LZ4 library for encoding or decoding.
class RingBuffer {
 public:
  RingBuffer(size_t buffer_size);

  char* GetPtr() const { return ring_ptr_; }

  // Updates the ring's pointer position.
  void Advance(size_t pos);

  // Writes the chunk data and updates the ring's pointer.
  //
  // The return value points to the ring address where the copy was written to.
  char* Write(const char* chunk, size_t chunk_size);

  void Reset() { ring_ptr_ = ring_start_; }

 private:
  std::vector<char> ring_buffer_;
  const size_t write_size_;
  char* ring_ptr_;
  char* ring_start_;
  char* ring_end_;
};

}  // namespace system_log_recorder
}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_SYSTEM_LOG_RECORDER_ENCODING_RING_BUFFER_H_
