// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/fdio/vfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/time.h>
#include <zircon/types.h>

#include <ctime>
#include <optional>

#include <fbl/algorithm.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using AttrTest = FilesystemTest;

zx_time_t ToNanoSeconds(std::timespec ts) {
  // assumes very small number of seconds in deltas
  return zx_time_from_timespec(ts);
}

std::optional<zx_time_t> GetCurrentTimeNanos() {
  std::timespec ts;
  if (!std::timespec_get(&ts, TIME_UTC)) {
    return std::nullopt;
  }
  return ToNanoSeconds(ts);
}

TEST_P(AttrTest, SetModificationTime) {
  auto now_opt = GetCurrentTimeNanos();
  ASSERT_TRUE(now_opt) << "Failed to fetch the current time";
  zx_time_t now = *now_opt;

  const std::string file = GetPath("file.txt");
  int fd1 = open(file.c_str(), O_CREAT | O_RDWR, 0644);
  ASSERT_GT(fd1, 0);

  std::timespec ts[2];
  ts[0].tv_nsec = UTIME_OMIT;
  ts[1].tv_sec = (long)(now / ZX_SEC(1));
  ts[1].tv_nsec = (long)(now % ZX_SEC(1));

  // make sure we get back "now" from stat()
  ASSERT_EQ(futimens(fd1, ts), 0);
  struct stat statb1;
  ASSERT_EQ(fstat(fd1, &statb1), 0);
  now = fbl::round_down(static_cast<uint64_t>(now),
                        static_cast<uint64_t>(fs().GetTraits().timestamp_granularity.to_nsecs()));
  ASSERT_EQ(statb1.st_mtim.tv_sec, (long)(now / ZX_SEC(1)));
  ASSERT_EQ(statb1.st_mtim.tv_nsec, (long)(now % ZX_SEC(1)));
  ASSERT_EQ(close(fd1), 0);

  ASSERT_EQ(unlink(file.c_str()), 0);
}

TEST_P(AttrTest, Utimes) {
  auto now_opt = GetCurrentTimeNanos();
  ASSERT_TRUE(now_opt) << "Failed to fetch the current time";
  zx_time_t now = *now_opt;

  const std::string file = GetPath("file.txt");
  int fd1 = open(file.c_str(), O_CREAT | O_RDWR, 0644);
  EXPECT_GT(fd1, 0);

  std::timespec ts[2];
  ts[0].tv_nsec = UTIME_OMIT;
  ts[1].tv_sec = (long)(now / ZX_SEC(1));
  ts[1].tv_nsec = (long)(now % ZX_SEC(1));

  // Make sure we get back "now" from stat().
  EXPECT_EQ(futimens(fd1, ts), 0);
  struct stat statb1;
  EXPECT_EQ(fstat(fd1, &statb1), 0);
  now = fbl::round_down(static_cast<uint64_t>(now),
                        static_cast<uint64_t>(fs().GetTraits().timestamp_granularity.to_nsecs()));
  EXPECT_EQ(statb1.st_mtim.tv_sec, (long)(now / ZX_SEC(1)));
  EXPECT_EQ(statb1.st_mtim.tv_nsec, (long)(now % ZX_SEC(1)));
  EXPECT_EQ(close(fd1), 0);

  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));

  ASSERT_EQ(utimes(file.c_str(), nullptr), 0);
  struct stat statb2;
  ASSERT_EQ(stat(file.c_str(), &statb2), 0);
  ASSERT_GT(ToNanoSeconds(statb2.st_mtim), ToNanoSeconds(statb1.st_mtim));

  ASSERT_EQ(unlink(file.c_str()), 0);
}

TEST_P(AttrTest, WriteSetsModificationTime) {
  const std::string file = GetPath("file.txt");
  int fd1 = open(file.c_str(), O_CREAT | O_RDWR, 0644);
  EXPECT_GT(fd1, 0);

  struct stat stat1, stat2;
  EXPECT_EQ(fstat(fd1, &stat1), 0);

  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
  char buffer[100];
  memset(buffer, 'a', sizeof(buffer));
  ssize_t ret = write(fd1, buffer, sizeof(buffer));
  EXPECT_GT(ret, 0);
  EXPECT_EQ((unsigned long)(ret), sizeof(buffer));
  EXPECT_EQ(close(fd1), 0);
  ASSERT_EQ(stat(file.c_str(), &stat2), 0);

  ASSERT_LT(ToNanoSeconds(stat1.st_mtim), ToNanoSeconds(stat2.st_mtim));
  EXPECT_EQ(unlink(file.c_str()), 0);
}

TEST_P(AttrTest, WriteSetsModificationTimeNoClose) {
  const std::string file = GetPath("file.txt");
  int fd1 = open(file.c_str(), O_CREAT | O_RDWR, 0644);
  EXPECT_GT(fd1, 0);

  struct stat stat1, stat2;
  EXPECT_EQ(fstat(fd1, &stat1), 0);

  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
  char buffer[100];
  memset(buffer, 'a', sizeof(buffer));

  ssize_t ret = write(fd1, buffer, sizeof(buffer));
  EXPECT_GT(ret, 0);
  EXPECT_EQ((unsigned long)(ret), sizeof(buffer));
  ASSERT_EQ(stat(file.c_str(), &stat2), 0);

  ASSERT_LT(ToNanoSeconds(stat1.st_mtim), ToNanoSeconds(stat2.st_mtim));
  EXPECT_EQ(close(fd1), 0);
  EXPECT_EQ(unlink(file.c_str()), 0);
}

TEST_P(AttrTest, StatReturnsCorrectBlockSize) {
  const std::string file = GetPath("file.txt");
  int fd = open(file.c_str(), O_CREAT | O_RDWR, 0644);
  ASSERT_GT(fd, 0);

  struct stat buf;
  ASSERT_EQ(fstat(fd, &buf), 0);
  ASSERT_GT(buf.st_blksize, 0) << "blksize should be greater than zero";
  ASSERT_EQ(buf.st_blksize % VNATTR_BLKSIZE, 0) << "blksize should be a multiple of VNATTR_BLKSIZE";
  ASSERT_EQ(buf.st_blocks, 0) << "Number of allocated blocks should be zero";

  char data = {'a'};
  ASSERT_EQ(write(fd, &data, 1), 1) << "Couldn't write a single byte to file";
  ASSERT_EQ(fstat(fd, &buf), 0);
  ASSERT_GT(buf.st_blksize, 0) << "blksize should be greater than zero";
  ASSERT_EQ(buf.st_blksize % VNATTR_BLKSIZE, 0) << "blksize should be a multiple of VNATTR_BLKSIZE";
  ASSERT_GT(buf.st_blocks, 0) << "Number of allocated blocks should greater than zero";
  ASSERT_EQ(close(fd), 0);

  blkcnt_t nblocks = buf.st_blocks;
  ASSERT_EQ(stat(file.c_str(), &buf), 0);
  ASSERT_EQ(buf.st_blocks, nblocks) << "Block count changed when closing file";

  ASSERT_EQ(unlink(file.c_str()), 0);
}

TEST_P(AttrTest, ParentModificationTimeUpdatedCorrectly) {
  auto now_opt = GetCurrentTimeNanos();
  ASSERT_TRUE(now_opt) << "Failed to fetch the current time";
  zx_time_t now = *now_opt;

  // Create a parent directory to contain new contents
  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
  const std::string parent = GetPath("parent");
  const std::string parent2 = GetPath("parent2");
  const std::string child = GetPath("parent/child");
  const std::string child2 = GetPath("parent2/child");
  ASSERT_EQ(mkdir(parent.c_str(), 0666), 0);
  ASSERT_EQ(mkdir(parent2.c_str(), 0666), 0);

  // Ensure the parent directory's create + modified times
  // were initialized correctly.
  struct stat statb;
  ASSERT_EQ(stat(parent.c_str(), &statb), 0);
  ASSERT_GT(ToNanoSeconds(statb.st_ctim), now);
  ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
  now = ToNanoSeconds(statb.st_ctim);

  // Create a file in the parent directory
  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
  int fd = open(child.c_str(), O_CREAT | O_RDWR);
  ASSERT_GT(fd, 0);
  ASSERT_EQ(close(fd), 0);

  // Time moved forward in both the child...
  ASSERT_EQ(stat(child.c_str(), &statb), 0);
  ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
  // ... and the parent
  ASSERT_EQ(stat(parent.c_str(), &statb), 0);
  ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
  now = ToNanoSeconds(statb.st_mtim);

  // Don't test links on filesystems with no hard link support.
  if (fs().GetTraits().supports_hard_links) {
    // Link the child into a second directory
    zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
    ASSERT_EQ(link(child.c_str(), child2.c_str()), 0);
    // Source directory is not impacted
    ASSERT_EQ(stat(parent.c_str(), &statb), 0);
    ASSERT_EQ(ToNanoSeconds(statb.st_mtim), now);
    // Target directory is updated
    ASSERT_EQ(stat(parent2.c_str(), &statb), 0);
    ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
    now = ToNanoSeconds(statb.st_mtim);

    // Unlink the child, and the parent's time should
    // move forward again
    zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
    ASSERT_EQ(unlink(child2.c_str()), 0);
    ASSERT_EQ(stat(parent2.c_str(), &statb), 0);
    ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
    now = ToNanoSeconds(statb.st_mtim);
  }

  // Rename the child, and both the source and dest
  // directories should be updated
  zx_nanosleep(zx_deadline_after(fs().GetTraits().timestamp_granularity.to_nsecs()));
  ASSERT_EQ(rename(child.c_str(), child2.c_str()), 0);
  ASSERT_EQ(stat(parent.c_str(), &statb), 0);
  ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);
  ASSERT_EQ(stat(parent2.c_str(), &statb), 0);
  ASSERT_GT(ToNanoSeconds(statb.st_mtim), now);

  // Clean up
  ASSERT_EQ(unlink(child2.c_str()), 0);
  ASSERT_EQ(rmdir(parent2.c_str()), 0);
  ASSERT_EQ(rmdir(parent.c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, AttrTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
