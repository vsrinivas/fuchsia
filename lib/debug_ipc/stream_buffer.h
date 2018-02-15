// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <vector>

// This class is a buffer that sits between an asynchronous OS data source and
// a consumer of that stream. The data source supplies data in chunks which
// are buffered
class StreamBuffer {
 public:
  StreamBuffer();
  ~StreamBuffer();

  void AddData(std::vector<char> data);

  // Returns true if the given number of bytes are available for reading.
  bool IsAvailable(size_t count) const;

  // Copies up to |buffer_len| bytes to the given |buffer|. If there is not
  // enough data available it will do a partial read (check IsAvailable() if
  // you need to know in advance). Returns the number of bytes actually read.
  size_t Read(char* buffer, size_t buffer_len);

  // Like Read() but does not actually consume the data. The same data will be
  // supplied for a subsequent Peek() or Read() call.
  size_t Peek(char* buffer, size_t buffer_len) const;

 private:
  size_t ReadOrPeek(char* buffer, size_t buffer_len, bool erase_consumed);

  // Stores the buffered data in a sequence of ordered buffers. Read at the
  // front, add data at the back.
  std::deque<const std::vector<char>> buffers_;
  size_t first_node_offset_ = 0;  // Read position of buffer_[0].
};
