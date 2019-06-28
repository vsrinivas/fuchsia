// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_SHARED_TEST_STREAM_BUFFER_H_
#define SRC_DEVELOPER_DEBUG_SHARED_TEST_STREAM_BUFFER_H_

#include <deque>

#include "src/developer/debug/shared/stream_buffer.h"
#include "src/lib/fxl/macros.h"

namespace debug_ipc {

// An implementation of StreamBuffer that provides the simplest-possible buffering to memory for
// test purposes.
//
// The stream buffer is bidirectional and has a buffer going both ways:
//
//   Writing to the stream buffer: stream().Write(...)
//   will come out in write_sink().
//
//   Reading from the stream buffer: stream().Read(...) or ...Peek();
//   data is provided by stream().AddReadData(...).
class TestStreamBuffer : public StreamBuffer::Writer {
 public:
  TestStreamBuffer();
  ~TestStreamBuffer();

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

  // Where data that is written to the stream buffer ends up. This emulates what would normally be
  // the system-specific destination (file, etc.).
  const std::deque<char>& write_sink() const { return write_sink_; }
  std::deque<char>& write_sink() { return write_sink_; }

 private:
  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  StreamBuffer stream_;

  std::deque<char> write_sink_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestStreamBuffer);
};

}  // namespace debug_ipc

#endif  // SRC_DEVELOPER_DEBUG_SHARED_TEST_STREAM_BUFFER_H_
