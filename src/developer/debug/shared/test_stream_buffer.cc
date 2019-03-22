// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/test_stream_buffer.h"

namespace debug_ipc {

TestStreamBuffer::TestStreamBuffer() { stream_.set_writer(this); }

TestStreamBuffer::~TestStreamBuffer() = default;

size_t TestStreamBuffer::ConsumeStreamBufferData(const char* data, size_t len) {
  write_sink_.insert(write_sink_.end(), &data[0], &data[len]);
  return len;
}

}  // namespace debug_ipc
