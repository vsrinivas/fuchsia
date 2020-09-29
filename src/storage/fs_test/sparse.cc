// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <tuple>
#include <vector>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using ParamType =
    std::tuple<TestFilesystemOptions,
               std::tuple</*write_offset=*/size_t, /*read_offset=*/size_t, /*write_size=*/size_t>>;

class SparseTest : public BaseFilesystemTest, public testing::WithParamInterface<ParamType> {
 public:
  SparseTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  size_t write_offset() const { return std::get<0>(std::get<1>(GetParam())); }
  size_t read_offset() const { return std::get<1>(std::get<1>(GetParam())); }
  size_t write_size() const { return std::get<2>(std::get<1>(GetParam())); }
};

TEST_P(SparseTest, ReadAfterSparseWriteReturnsCorrectData) {
  const std::string my_file = GetPath("my_file");
  fbl::unique_fd fd(open(my_file.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  // Create a random write buffer of data
  std::vector<uint8_t> wbuf(write_size());
  unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
  std::cerr << "Sparse test using seed: " << seed << std::endl;
  for (size_t i = 0; i < write_size(); i++) {
    wbuf[i] = (uint8_t)rand_r(&seed);
  }

  // Dump write buffer to file
  ASSERT_EQ(pwrite(fd.get(), &wbuf[0], write_size(), write_offset()),
            static_cast<ssize_t>(write_size()));

  // Reopen file
  ASSERT_EQ(close(fd.release()), 0);
  fd.reset(open(my_file.c_str(), O_RDWR, 0644));
  ASSERT_TRUE(fd);

  // Access read buffer from file
  const size_t file_size = write_offset() + write_size();
  const size_t bytes_to_read =
      (file_size - read_offset()) > write_size() ? write_size() : (file_size - read_offset());
  ASSERT_GT(bytes_to_read, 0ul) << "We want to test writing AND reading";
  std::vector<uint8_t> rbuf(bytes_to_read);
  ASSERT_EQ(pread(fd.get(), &rbuf[0], bytes_to_read, read_offset()),
            static_cast<ssize_t>(bytes_to_read));

  const size_t sparse_length =
      (read_offset() < write_offset()) ? write_offset() - read_offset() : 0;

  if (sparse_length > 0) {
    for (size_t i = 0; i < sparse_length; i++) {
      ASSERT_EQ(rbuf[i], 0) << "This portion of file should be sparse; but isn't";
    }
  }

  const size_t wbuf_offset = (read_offset() < write_offset()) ? 0 : read_offset() - write_offset();
  const size_t valid_length = bytes_to_read - sparse_length;

  if (valid_length > 0) {
    for (size_t i = 0; i < valid_length; i++) {
      ASSERT_EQ(rbuf[sparse_length + i], wbuf[wbuf_offset + i]);
    }
  }

  // Clean up
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(my_file.c_str()), 0);
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << "WithWriteOffset" << std::get<0>(std::get<1>(param.param))
    << "ReadOffset" << std::get<1>(std::get<1>(param.param)) << "WriteSize"
    << std::get<2>(std::get<1>(param.param));
  return s.str();
}

std::vector<TestFilesystemOptions> AllTestFilesystemsWithCustomDisk() {
  std::vector<TestFilesystemOptions> filesystems;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    options.device_block_count = 1LLU << 24;
    options.device_block_size = 1LLU << 9;
    options.fvm_slice_size = 1LLU << 23;
    options.zero_fill = true;
    filesystems.push_back(options);
  }
  return filesystems;
}

constexpr size_t kBlockSize = 8192;
constexpr size_t kDirectBlocks = 16;

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, SparseTest,
    testing::Combine(
        testing::ValuesIn(AllTestFilesystemsWithCustomDisk()),
        testing::Values(std::make_tuple(0, 0, kBlockSize),
                        std::make_tuple(kBlockSize / 2, 0, kBlockSize),
                        std::make_tuple(kBlockSize / 2, kBlockSize, kBlockSize),
                        std::make_tuple(kBlockSize, 0, kBlockSize),
                        std::make_tuple(kBlockSize, kBlockSize / 2, kBlockSize),
                        std::make_tuple(kBlockSize* kDirectBlocks,
                                        kBlockSize* kDirectBlocks - kBlockSize, kBlockSize * 2),
                        std::make_tuple(kBlockSize* kDirectBlocks,
                                        kBlockSize* kDirectBlocks - kBlockSize, kBlockSize * 32),
                        std::make_tuple(kBlockSize* kDirectBlocks + kBlockSize,
                                        kBlockSize* kDirectBlocks - kBlockSize, kBlockSize * 32),
                        std::make_tuple(kBlockSize* kDirectBlocks + kBlockSize,
                                        kBlockSize* kDirectBlocks + 2 * kBlockSize,
                                        kBlockSize * 32))),
    GetParamDescription);

}  // namespace
}  // namespace fs_test
