// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <algorithm>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "fs_test_fixture.h"

namespace fs_test {
namespace {

using ParamType = std::tuple<TestFilesystemOptions, /*remount=*/bool>;

constexpr int kMb = 1 << 20;
constexpr int kPrintSize = 100 * kMb;

class MaxFileTest : public BaseFilesystemTest, public testing::WithParamInterface<ParamType> {
 public:
  MaxFileTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  bool ShouldRemount() const { return std::get<1>(GetParam()); }
};

// Test writing as much as we can to a file until we run out of space.
TEST_P(MaxFileTest, ReadAfterWriteMaxFileSucceeds) {
  // TODO(ZX-1735): We avoid making files that consume more than half
  // of physical memory. When we can page out files, this restriction
  // should be removed.
  const size_t physmem = zx_system_get_physmem();
  const size_t max_cap = physmem / 2;

  fbl::unique_fd fd(open(GetPath("bigfile").c_str(), O_CREAT | O_RDWR, 0644));
  ASSERT_TRUE(fd);
  char data_a[8192];
  char data_b[8192];
  char data_c[8192];
  memset(data_a, 0xaa, sizeof(data_a));
  memset(data_b, 0xbb, sizeof(data_b));
  memset(data_c, 0xcc, sizeof(data_c));
  size_t sz = 0;
  ssize_t r;

  auto rotate = [&](const char* data) {
    if (data == data_a) {
      return data_b;
    } else if (data == data_b) {
      return data_c;
    } else {
      return data_a;
    }
  };

  const char* data = data_a;
  for (;;) {
    if (sz >= max_cap) {
      FX_LOGS(INFO) << "Approaching physical memory capacity: " << sz << " bytes";
      r = 0;
      break;
    }

    if ((r = write(fd.get(), data, sizeof(data_a))) < 0) {
      FX_LOGS(INFO) << "bigfile received error: " << strerror(errno);
      if ((errno == EFBIG) || (errno == ENOSPC)) {
        // Either the file should be too big (EFBIG) or the file should
        // consume the whole volume (ENOSPC).
        FX_LOGS(INFO) << "(This was an expected error)";
        r = 0;
      }
      break;
    }
    if ((sz + r) % kPrintSize < (sz % kPrintSize)) {
      FX_LOGS(INFO) << "wrote " << (sz + r) / kMb << " MB";
    }
    sz += r;
    ASSERT_EQ(r, static_cast<ssize_t>(sizeof(data_a)));

    // Rotate which data buffer we use
    data = rotate(data);
  }
  ASSERT_EQ(r, 0) << "Saw an unexpected error from write";
  FX_LOGS(INFO) << "wrote " << sz << " bytes";

  struct stat buf;
  ASSERT_EQ(fstat(fd.get(), &buf), 0);
  ASSERT_EQ(buf.st_size, static_cast<ssize_t>(sz));

  // Try closing, re-opening, and verifying the file
  ASSERT_EQ(close(fd.release()), 0);
  if (ShouldRemount()) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }
  fd.reset(open(GetPath("bigfile").c_str(), O_RDWR, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(fstat(fd.get(), &buf), 0);
  ASSERT_EQ(buf.st_size, static_cast<ssize_t>(sz));
  char readbuf[8192];
  size_t bytes_read = 0;
  data = data_a;
  while (bytes_read < sz) {
    r = read(fd.get(), readbuf, sizeof(readbuf));
    ASSERT_EQ(r, static_cast<ssize_t>(std::min(sz - bytes_read, sizeof(readbuf))));
    ASSERT_EQ(memcmp(readbuf, data, r), 0);
    data = rotate(data);
    bytes_read += r;
  }

  ASSERT_EQ(bytes_read, sz);

  ASSERT_EQ(unlink(GetPath("bigfile").c_str()), 0);
  ASSERT_EQ(close(fd.release()), 0);
}

// Test writing to two files, in alternation, until we run out of space. For trivial (sequential)
// block allocation policies, this will create two large files with non-contiguous block
// allocations.
TEST_P(MaxFileTest, ReadAfterNonContiguousWritesSuceeds) {
  // TODO(ZX-1735): We avoid making files that consume more than half
  // of physical memory. When we can page out files, this restriction
  // should be removed.
  const size_t physmem = zx_system_get_physmem();
  const size_t max_cap = physmem / 4;

  fbl::unique_fd fda(open(GetPath("bigfile-A").c_str(), O_CREAT | O_RDWR, 0644));
  fbl::unique_fd fdb(open(GetPath("bigfile-B").c_str(), O_CREAT | O_RDWR, 0644));
  ASSERT_TRUE(fda);
  ASSERT_TRUE(fdb);
  char data_a[8192];
  char data_b[8192];
  memset(data_a, 0xaa, sizeof(data_a));
  memset(data_b, 0xbb, sizeof(data_b));
  size_t sz_a = 0;
  size_t sz_b = 0;
  ssize_t r;

  size_t* sz = &sz_a;
  int fd = fda.get();
  const char* data = data_a;
  for (;;) {
    if (*sz >= max_cap) {
      FX_LOGS(INFO) << "Approaching physical memory capacity: " << *sz << " bytes";
      r = 0;
      break;
    }

    if ((r = write(fd, data, sizeof(data_a))) <= 0) {
      FX_LOGS(INFO) << "bigfile received error: " << strerror(errno);
      // Either the file should be too big (EFBIG) or the file should
      // consume the whole volume (ENOSPC).
      ASSERT_TRUE(errno == EFBIG || errno == ENOSPC);
      FX_LOGS(INFO) << "(This was an expected error)";
      break;
    }
    if ((*sz + r) % kPrintSize < (*sz % kPrintSize)) {
      FX_LOGS(INFO) << "wrote " << (*sz + r) / kMb << " MB";
    }
    *sz += r;
    ASSERT_EQ(r, static_cast<ssize_t>(sizeof(data_a)));

    fd = (fd == fda.get()) ? fdb.get() : fda.get();
    data = (data == data_a) ? data_b : data_a;
    sz = (sz == &sz_a) ? &sz_b : &sz_a;
  }
  FX_LOGS(INFO) << "wrote " << sz_a << " bytes (to A)";
  FX_LOGS(INFO) << "wrote " << sz_b << " bytes (to B)";

  struct stat buf;
  ASSERT_EQ(fstat(fda.get(), &buf), 0);
  ASSERT_EQ(buf.st_size, static_cast<ssize_t>(sz_a));
  ASSERT_EQ(fstat(fdb.get(), &buf), 0);
  ASSERT_EQ(buf.st_size, static_cast<ssize_t>(sz_b));

  // Try closing, re-opening, and verifying the file
  ASSERT_EQ(close(fda.release()), 0);
  ASSERT_EQ(close(fdb.release()), 0);
  if (ShouldRemount()) {
    EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
    EXPECT_EQ(fs().Mount().status_value(), ZX_OK);
  }
  fda.reset(open(GetPath("bigfile-A").c_str(), O_RDWR, 0644));
  fdb.reset(open(GetPath("bigfile-B").c_str(), O_RDWR, 0644));
  ASSERT_TRUE(fda);
  ASSERT_TRUE(fdb);

  char readbuf[8192];
  size_t bytes_read_a = 0;
  size_t bytes_read_b = 0;

  fd = fda.get();
  data = data_a;
  sz = &sz_a;
  size_t* bytes_read = &bytes_read_a;
  while (*bytes_read < *sz) {
    r = read(fd, readbuf, sizeof(readbuf));
    ASSERT_EQ(r, static_cast<ssize_t>(std::min(*sz - *bytes_read, sizeof(readbuf))));
    ASSERT_EQ(memcmp(readbuf, data, r), 0);
    *bytes_read += r;

    fd = (fd == fda.get()) ? fdb.get() : fda.get();
    data = (data == data_a) ? data_b : data_a;
    sz = (sz == &sz_a) ? &sz_b : &sz_a;
    bytes_read = (bytes_read == &bytes_read_a) ? &bytes_read_b : &bytes_read_a;
  }

  ASSERT_EQ(bytes_read_a, sz_a);
  ASSERT_EQ(bytes_read_b, sz_b);

  ASSERT_EQ(unlink(GetPath("bigfile-A").c_str()), 0);
  ASSERT_EQ(unlink(GetPath("bigfile-B").c_str()), 0);
  ASSERT_EQ(close(fda.release()), 0);
  ASSERT_EQ(close(fdb.release()), 0);
}

std::string GetParamDescription(const testing::TestParamInfo<ParamType>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << (std::get<1>(param.param) ? "WithRemount" : "WithoutRemount");
  return s.str();
}

std::vector<ParamType> GetTestCombinations() {
  std::vector<ParamType> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    // Use a larger ram-disk than the default so that the maximum transaction limit is exceeded for
    // during delayed data allocation on non-FVM-backed Minfs partitions.
    options.device_block_size = 512;
    options.device_block_count = 1'048'576;
    options.fvm_slice_size = 8'388'608;
    test_combinations.push_back(ParamType{options, false});
    if (options.filesystem->GetTraits().can_unmount) {
      test_combinations.push_back(ParamType{options, true});
    }
  }
  return test_combinations;
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MaxFileTest, testing::ValuesIn(GetTestCombinations()),
                         GetParamDescription);

}  // namespace
}  // namespace fs_test
