// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_TEST_PIPE_READER_H_
#define SRC_LIB_ZXDUMP_TEST_PIPE_READER_H_

#include <unistd.h>

#include <string>
#include <thread>

#include <fbl/unique_fd.h>

namespace zxdump::testing {

class TestPipeReader {
 public:
  // This creates a pipe and yields the write half.
  void Init(fbl::unique_fd& write_pipe);

  // This must be called before destruction and nothing else after it.
  std::string Finish() && {
    thread_.join();
    return std::move(contents_);
  }

  ~TestPipeReader();

 private:
  void ReaderThread();

  std::string contents_;
  fbl::unique_fd read_pipe_;
  size_t pipe_buf_size_;
  std::thread thread_;
};

}  // namespace zxdump::testing

#endif  // SRC_LIB_ZXDUMP_TEST_PIPE_READER_H_
