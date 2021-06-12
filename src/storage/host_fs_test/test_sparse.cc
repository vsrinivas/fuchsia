// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <time.h>

#include <iostream>
#include <vector>

#include "src/storage/host_fs_test/fixture.h"

namespace fs_test {
namespace {

struct TestParam {
  size_t write_offset;
  size_t read_offset;
  size_t write_size;
};

class SparseHostFilesystemTest : public HostFilesystemTest,
                                 public testing::WithParamInterface<TestParam> {};

static unsigned count = 0;
TEST_P(SparseHostFilesystemTest, Sparse) {
  char filename[20];
  sprintf(filename, "::my_file_%u", ++count);

  int fd = emu_open(filename, O_RDWR | O_CREAT, 0644);
  ASSERT_GT(fd, 0);

  // Create a random write buffer of data
  std::vector<uint8_t> wbuf(GetParam().write_size);
  unsigned int seed = static_cast<unsigned int>(time(nullptr));
  std::cerr << "Sparse test using seed: " << seed << std::endl;
  for (size_t i = 0; i < GetParam().write_size; ++i) {
    wbuf[i] = (uint8_t)rand_r(&seed);
  }

  // Dump write buffer to file
  ASSERT_EQ(emu_pwrite(fd, wbuf.data(), GetParam().write_size, GetParam().write_offset),
            static_cast<ssize_t>(GetParam().write_size));
  // Reopen file
  ASSERT_EQ(emu_close(fd), 0);
  fd = emu_open(filename, O_RDWR, 0644);
  ASSERT_GT(fd, 0);

  // Access read buffer from file
  const size_t file_size = GetParam().write_offset + GetParam().write_size;
  const size_t bytes_to_read = (file_size - GetParam().read_offset) > GetParam().write_size
                                   ? GetParam().write_size
                                   : (file_size - GetParam().read_offset);
  ASSERT_GT(bytes_to_read, 0u) << "We want to test writing AND reading";
  std::vector<uint8_t> rbuf(bytes_to_read);
  ASSERT_EQ(emu_pread(fd, rbuf.data(), bytes_to_read, GetParam().read_offset),
            static_cast<ssize_t>(bytes_to_read));

  const size_t sparse_length = (GetParam().read_offset < GetParam().write_offset)
                                   ? GetParam().write_offset - GetParam().read_offset
                                   : 0;

  if (sparse_length > 0) {
    for (size_t i = 0; i < sparse_length; ++i) {
      ASSERT_EQ(rbuf[i], 0) << "This portion of file should be sparse; but isn't";
    }
  }

  const size_t wbuf_offset = (GetParam().read_offset < GetParam().write_offset)
                                 ? 0
                                 : GetParam().read_offset - GetParam().write_offset;
  const size_t valid_length = bytes_to_read - sparse_length;

  if (valid_length > 0) {
    for (size_t i = 0; i < valid_length; ++i) {
      ASSERT_EQ(rbuf[sparse_length + i], wbuf[wbuf_offset + i]);
    }
  }

  ASSERT_EQ(emu_close(fd), 0);
  ASSERT_EQ(RunFsck(), 0);
}

constexpr size_t kBlockSize = 8192;
constexpr size_t kDirectBlocks = 16;

std::string GetParamDescription(const testing::TestParamInfo<TestParam>& param) {
  std::stringstream s;
  s << "WriteOffset" << param.param.write_offset << "ReadOffset" << param.param.read_offset
    << "WriteSize" << param.param.write_size;
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, SparseHostFilesystemTest,
    testing::Values(TestParam{kBlockSize / 2, 0, kBlockSize},
                    TestParam{kBlockSize / 2, kBlockSize, kBlockSize},
                    TestParam{kBlockSize, 0, kBlockSize},
                    TestParam{kBlockSize, kBlockSize / 2, kBlockSize},
                    TestParam{kBlockSize * kDirectBlocks, kBlockSize* kDirectBlocks - kBlockSize,
                              kBlockSize * 2},
                    TestParam{kBlockSize * kDirectBlocks, kBlockSize* kDirectBlocks - kBlockSize,
                              kBlockSize * 32},
                    TestParam{kBlockSize * kDirectBlocks + kBlockSize,
                              kBlockSize* kDirectBlocks - kBlockSize, kBlockSize * 32},
                    TestParam{kBlockSize * kDirectBlocks + kBlockSize,
                              kBlockSize* kDirectBlocks + 2 * kBlockSize, kBlockSize * 32}),
    GetParamDescription);

}  // namespace
}  // namespace fs_test
