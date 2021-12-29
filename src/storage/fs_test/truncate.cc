// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <random>
#include <tuple>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/truncate_fixture.h"

namespace fs_test {
namespace {

using TruncateTest = FilesystemTest;

void CheckFileContains(const char* filename, const void* data, ssize_t len) {
  char buf[4096];
  struct stat st;

  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, len);
  fbl::unique_fd fd(open(filename, O_RDWR, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(read(fd.get(), buf, len), len);
  ASSERT_EQ(memcmp(buf, data, len), 0);
}

void CheckFileEmpty(const char* filename) {
  struct stat st;
  ASSERT_EQ(stat(filename, &st), 0);
  ASSERT_EQ(st.st_size, 0);
}

// Test that the really simple cases of truncate are operational
TEST_P(TruncateTest, TruncateSmall) {
  const char* str = "Hello, World!\n";
  const std::string filename = GetPath("alpha");

  // Try writing a string to a file
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(write(fd.get(), str, strlen(str)), static_cast<ssize_t>(strlen(str)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename.c_str(), str, strlen(str)));

  // Check that opening a file with O_TRUNC makes it empty
  fbl::unique_fd fd2(open(filename.c_str(), O_RDWR | O_TRUNC, 0644));
  ASSERT_TRUE(fd2);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename.c_str()));

  // Check that we can still write to a file that has been truncated
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), str, strlen(str)), static_cast<ssize_t>(strlen(str)));
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename.c_str(), str, strlen(str)));

  // Check that we can truncate the file using the "truncate" function
  ASSERT_EQ(truncate(filename.c_str(), 5), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename.c_str(), str, 5));
  ASSERT_EQ(truncate(filename.c_str(), 0), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename.c_str()));

  // Check that truncating an already empty file does not cause problems
  ASSERT_EQ(truncate(filename.c_str(), 0), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileEmpty(filename.c_str()));

  // Check that we can use truncate to extend a file
  char empty[5] = {0, 0, 0, 0, 0};
  ASSERT_EQ(truncate(filename.c_str(), 5), 0);
  ASSERT_NO_FATAL_FAILURE(CheckFileContains(filename.c_str(), empty, 5));

  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

enum class SparseTestType {
  UnlinkThenClose,
  CloseThenUnlink,
};

using ParamType = std::tuple<TestFilesystemOptions, SparseTestType>;

class SparseTruncateTest : public BaseFilesystemTest,
                           public testing::WithParamInterface<ParamType> {
 public:
  SparseTruncateTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  SparseTestType test_type() const { return std::get<1>(GetParam()); }
};

// This test catches a particular regression in MinFS truncation, where, if a block is cut in half
// for truncation, it is read, filled with zeroes, and written back out to disk.
//
// This test tries to proke at a variety of offsets of interest.
TEST_P(SparseTruncateTest, PartialBlockSparse) {
  // TODO(smklein): Acquire these constants directly from MinFS's header
  constexpr size_t kBlockSize = 8192;
  constexpr size_t kDirectBlocks = 16;
  constexpr size_t kIndirectBlocks = 31;
  constexpr size_t kDirectPerIndirect = kBlockSize / 4;

  uint8_t buf[kBlockSize];
  memset(buf, 0xAB, sizeof(buf));

  off_t write_offsets[] = {
      kBlockSize * 5,
      kBlockSize * kDirectBlocks,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 1,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * 2,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks -
          2 * kBlockSize,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks - kBlockSize,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks,
      kBlockSize * kDirectBlocks + kBlockSize * kDirectPerIndirect * kIndirectBlocks + kBlockSize,
  };

  for (size_t i = 0; i < std::size(write_offsets); i++) {
    off_t write_off = write_offsets[i];
    fbl::unique_fd fd(open(GetPath("truncate-sparse").c_str(), O_CREAT | O_RDWR));
    ASSERT_TRUE(fd);
    ASSERT_EQ(lseek(fd.get(), write_off, SEEK_SET), write_off);
    ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)))
        << "errno=" << errno << ", write_off=" << write_off;
    ASSERT_EQ(ftruncate(fd.get(), write_off + 2 * kBlockSize), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off + kBlockSize + kBlockSize / 2), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off + kBlockSize / 2), 0);
    ASSERT_EQ(ftruncate(fd.get(), write_off - kBlockSize / 2), 0);
    if (test_type() == SparseTestType::UnlinkThenClose) {
      ASSERT_EQ(unlink(GetPath("truncate-sparse").c_str()), 0);
      ASSERT_EQ(close(fd.release()), 0);
    } else {
      ASSERT_EQ(close(fd.release()), 0);
      ASSERT_EQ(unlink(GetPath("truncate-sparse").c_str()), 0);
    }
  }
}

TEST_P(TruncateTest, Errno) {
  fbl::unique_fd fd(open(GetPath("truncate_errno").c_str(), O_RDWR | O_CREAT | O_EXCL));
  ASSERT_TRUE(fd);

  ASSERT_EQ(ftruncate(fd.get(), -1), -1);
  ASSERT_EQ(errno, EINVAL);
  errno = 0;

  const off_t max_file_size = fs().GetTraits().max_file_size;
  if (max_file_size < std::numeric_limits<off_t>::max()) {
    ASSERT_EQ(ftruncate(fd.get(), max_file_size + 1), -1);
    ASSERT_EQ(errno, EINVAL);
  }

  ASSERT_EQ(unlink(GetPath("truncate_errno").c_str()), 0);
  ASSERT_EQ(close(fd.release()), 0);
}

TEST_P(TruncateTest, ShrinkRace) {
  std::string file = GetPath("truncate_shrink_race");
  const char* file_name = file.c_str();
  const uint32_t page_size = zx_system_get_page_size();
  const uint32_t offset = page_size - 2;
  const char data[] = "hello";
  const ssize_t len = static_cast<ssize_t>(strlen(data));
  const uint32_t end = offset + len;
  std::vector<uint8_t> zero(offset);
  for (int i = 0; i < 100; ++i) {
    {
      fbl::unique_fd fd(open(file_name, O_RDWR | O_CREAT | O_TRUNC, 0666));
      ASSERT_TRUE(fd) << strerror(errno);
      ASSERT_EQ(pwrite(fd.get(), data, len, offset), len);
      fd.reset();
    }
    std::thread thread1([&] {
      fbl::unique_fd fd(open(file_name, O_RDWR));
      ASSERT_TRUE(fd);
      std::random_device random;
      std::uniform_int_distribution distribution(0, 1000);
      usleep(distribution(random));
      const size_t buf_size = page_size * 2 + 100;
      char buf[buf_size];
      ssize_t result = read(fd.get(), buf, buf_size);
      EXPECT_TRUE(result == end || result == 0) << errno;
      if (result == end) {
        EXPECT_EQ(memcmp(buf, zero.data(), zero.size()), 0);
        EXPECT_EQ(memcmp(buf + offset, data, len), 0);
      }
    });
    std::thread thread2([&] {
      fbl::unique_fd fd(open(file_name, O_RDWR));
      ASSERT_TRUE(fd);
      EXPECT_EQ(ftruncate(fd.get(), 0), 0);
      EXPECT_EQ(fsync(fd.get()), 0);
    });
    thread1.join();
    thread2.join();
  }
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType> param) {
  std::stringstream s;
  s << std::get<0>(param.param);
  switch (std::get<1>(param.param)) {
    case SparseTestType::UnlinkThenClose:
      s << "UnlinkThenClose";
      break;
    case SparseTestType::CloseThenUnlink:
      s << "CloseThenUnlink";
      break;
  }
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, TruncateTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

// These tests will only work on a file system that supports sparse files.
INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, SparseTruncateTest,
    testing::Combine(
        testing::ValuesIn(MapAndFilterAllTestFilesystems(
            [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
              if (options.filesystem->GetTraits().supports_sparse_files) {
                return options;
              } else {
                return std::nullopt;
              }
            })),
        testing::Values(SparseTestType::UnlinkThenClose, SparseTestType::CloseThenUnlink)),
    GetParamDescription);

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(SparseTruncateTest);

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, LargeTruncateTest,
    testing::Combine(testing::ValuesIn(AllTestFilesystems()),
                     testing::Values(std::make_tuple(1 << 10, 100, LargeTruncateTestType::KeepOpen),
                                     std::make_tuple(1 << 10, 100, LargeTruncateTestType::Reopen),
                                     std::make_tuple(1 << 15, 50, LargeTruncateTestType::KeepOpen),
                                     std::make_tuple(1 << 15, 50, LargeTruncateTestType::Reopen))),
    GetDescriptionForLargeTruncateTestParamType);

}  // namespace
}  // namespace fs_test
