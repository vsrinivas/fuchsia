// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debug_ipc/stream_buffer.h"

#include <algorithm>

#include <string.h>

namespace debug_ipc {

StreamBuffer::StreamBuffer() = default;
StreamBuffer::~StreamBuffer() = default;

void StreamBuffer::AddData(std::vector<char> data) {
  buffers_.push_back(std::move(data));
}

bool StreamBuffer::IsAvailable(size_t count) const {
  if (count == 0)
    return true;

  size_t current_node_offset = first_node_offset_;
  for (const auto& cur : buffers_) {
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

size_t StreamBuffer::ReadOrPeek(char* buffer,
                                size_t buffer_len,
                                bool erase_consumed) {
  size_t buffer_pos = 0;

  auto cur = buffers_.begin();
  size_t current_node_offset = first_node_offset_;
  while (cur != buffers_.end() && buffer_pos < buffer_len) {
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
    while (buffers_.begin() != cur)
      buffers_.pop_front();
    first_node_offset_ = current_node_offset;
  }
  return buffer_pos;
}

}  // namespace debug_ipc
