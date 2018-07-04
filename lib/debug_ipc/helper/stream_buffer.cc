// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/helper/stream_buffer.h"

#include <algorithm>

#include <string.h>

namespace debug_ipc {

StreamBuffer::StreamBuffer() = default;
StreamBuffer::~StreamBuffer() = default;

void StreamBuffer::AddReadData(std::vector<char> data) {
  read_buffer_.push_back(std::move(data));
}

void StreamBuffer::SetWritable() {
  can_write_ = true;
  FlushWriteBuffer();
}

bool StreamBuffer::IsAvailable(size_t count) const {
  if (count == 0)
    return true;

  size_t current_node_offset = first_read_buffer_offset_;
  for (const auto& cur : read_buffer_) {
    size_t in_current = cur.size() - current_node_offset;
    if (count <= in_current)
      return true;
    count -= in_current;
    current_node_offset = 0;
  }
  return false;
}

size_t StreamBuffer::Read(char* buffer, size_t buffer_len) {
  return ReadOrPeek(buffer, buffer_len, true);
}

size_t StreamBuffer::Peek(char* buffer, size_t buffer_len) const {
  return const_cast<StreamBuffer*>(this)->ReadOrPeek(buffer, buffer_len, false);
}

void StreamBuffer::Write(std::vector<char> data) {
  write_buffer_.push_back(std::move(data));
  if (can_write_)
    FlushWriteBuffer();
}

size_t StreamBuffer::ReadOrPeek(char* buffer, size_t buffer_len,
                                bool erase_consumed) {
  size_t buffer_pos = 0;

  auto cur = read_buffer_.begin();
  size_t current_node_offset = first_read_buffer_offset_;
  while (cur != read_buffer_.end() && buffer_pos < buffer_len) {
    size_t in_current_block = cur->size() - current_node_offset;
    size_t to_copy = std::min(buffer_len - buffer_pos, in_current_block);

    memcpy(&buffer[buffer_pos], &(*cur)[current_node_offset], to_copy);
    buffer_pos += to_copy;
    current_node_offset += to_copy;

    if (to_copy == in_current_block) {
      // Consumed the last of this block, move to the next one.
      cur++;
      current_node_offset = 0;
    }
  }
  if (erase_consumed) {
    // Update the state to reflect these read bytes.
    while (read_buffer_.begin() != cur)
      read_buffer_.pop_front();
    first_read_buffer_offset_ = current_node_offset;
  }
  return buffer_pos;
}

void StreamBuffer::FlushWriteBuffer() {
  while (!write_buffer_.empty()) {
    const std::vector<char>& cur = write_buffer_.front();
    int32_t written = writer_->ConsumeStreamBufferData(
        &cur[first_write_buffer_offset_],
        cur.size() - first_write_buffer_offset_);
    first_write_buffer_offset_ += written;

    if (first_write_buffer_offset_ < cur.size()) {
      // Partial write, block until notified about more.
      can_write_ = false;
      return;
    }

    // Consumed all the data, advance to the next buffer.
    write_buffer_.pop_front();
    first_write_buffer_offset_ = 0;
  }
}

}  // namespace debug_ipc
