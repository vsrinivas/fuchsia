// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <vector>

namespace debug_ipc {

// This class is a buffer that sits between an asynchronous OS read/write
// source and producers and consumer of stream data.
class StreamBuffer {
 public:
  class Writer {
   public:
    // Consumes as much of the given data as possible, returning how many bytes
    // were consumed. If less than "len" bytes are written, the system will
    // notify the stream buffer when more data can be written via
    // SetWriteable().
    virtual size_t ConsumeStreamBufferData(const char* data, size_t len) = 0;
  };

  // You must call set_writer before using.
  StreamBuffer();
  ~StreamBuffer();

  // System API ----------------------------------------------------------------

  // Sets the writer which flushes write data to the OS.
  void set_writer(Writer* writer) { writer_ = writer; }

  // Provides data from the OS source for reading.
  void AddReadData(std::vector<char> data);

  // Notification from the OS that data can be written.
  void SetWritable();

  // Public API ----------------------------------------------------------------

  // Returns true if the given number of bytes are available for reading.
  bool IsAvailable(size_t count) const;

  // Copies up to |buffer_len| bytes to the given |buffer|. If there is not
  // enough data available it will do a partial read (check IsAvailable() if
  // you need to know in advance). Returns the number of bytes actually read.
  size_t Read(char* buffer, size_t buffer_len);

  // Like Read() but does not actually consume the data. The same data will be
  // supplied for a subsequent Peek() or Read() call.
  size_t Peek(char* buffer, size_t buffer_len) const;

  // Writes the data to the OS sink.
  void Write(std::vector<char> data);

 private:
  size_t ReadOrPeek(char* buffer, size_t buffer_len, bool erase_consumed);

  void FlushWriteBuffer();

  Writer* writer_ = nullptr;

  // Read buffer in a sequence of ordered buffers. Read at the
  // front, add data at the back.
  std::deque<const std::vector<char>> read_buffer_;
  size_t first_read_buffer_offset_ = 0;  // Position of read_buffer_[0].

  // Write buffer.
  std::deque<const std::vector<char>> write_buffer_;
  bool can_write_ = true;
  size_t first_write_buffer_offset_ = 0;  // Position of write_buffer_[0].
};

}  // namespace debug_ipc
