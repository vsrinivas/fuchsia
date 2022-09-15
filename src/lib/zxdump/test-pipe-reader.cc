// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test-pipe-reader.h"

#include <gtest/gtest.h>

namespace zxdump::testing {

constexpr char kFillByte = 0x55;

void TestPipeReader::Init(fbl::unique_fd& write_pipe) {
  int fds[2];
  ASSERT_EQ(0, pipe(fds)) << "pipe: " << strerror(errno);
  read_pipe_.reset(fds[0]);
  write_pipe.reset(fds[1]);
  pipe_buf_size_ = fpathconf(read_pipe_.get(), _PC_PIPE_BUF);
  thread_ = std::thread(&TestPipeReader::ReaderThread, this);
}

TestPipeReader::~TestPipeReader() { EXPECT_FALSE(thread_.joinable()); }

// The reader thread will append everything read from the pipe to the string.
void TestPipeReader::ReaderThread() {
  ssize_t n;
  do {
    size_t contents_size = contents_.size();
    contents_.append(pipe_buf_size_, kFillByte);
    n = read(read_pipe_.get(), &contents_[contents_size], pipe_buf_size_);
    ASSERT_GE(n, 0) << "read: " << strerror(errno);
    contents_.resize(contents_size + n);
  } while (n > 0);
}

}  // namespace zxdump::testing
