// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>

#include <algorithm>
#include <iostream>

#include <fbl/algorithm.h>

#include "src/storage/host_fs_test/fixture.h"

namespace fs_test {
namespace {

constexpr int kMiB = 1 << 20;
constexpr int kPrintSize = 100 * kMiB;

TEST_F(HostFilesystemTest, MaxFile) {
  int fd = emu_open("::bigfile", O_CREAT | O_RDWR, 0644);
  ASSERT_GT(fd, 0);
  constexpr int kBufSize = 131072;
  char data_a[kBufSize];
  char data_b[kBufSize];
  char data_c[kBufSize];
  memset(data_a, 0xaa, sizeof(data_a));
  memset(data_b, 0xbb, sizeof(data_b));
  memset(data_c, 0xcc, sizeof(data_c));
  ssize_t sz = 0;
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
    if ((r = emu_write(fd, data, sizeof(data_a))) < 0) {
      std::cerr << "bigfile received error: " << strerror(errno) << std::endl;
      if (errno == EFBIG || errno == ENOSPC) {
        // Either the file should be too big (EFBIG) or the file should
        // consume the whole volume (ENOSPC).
        std::cerr << "(This was an expected error)" << std::endl;
        r = 0;
      }
      break;
    }
    if ((sz + r) % kPrintSize < (sz % kPrintSize)) {
      std::cerr << "wrote " << (sz + r) / kMiB << " MiB" << std::endl;
    }
    sz += r;
    if (r < (ssize_t)(sizeof(data_a))) {
      std::cerr << "bigfile write short write of " << r << " bytes" << std::endl;
      break;
    }

    // Rotate which data buffer we use
    data = rotate(data);
  }
  ASSERT_EQ(r, 0) << "Saw an unexpected error from write";

  struct stat buf;
  ASSERT_EQ(emu_fstat(fd, &buf), 0);
  ASSERT_EQ(buf.st_size, sz);

  // Try closing, re-opening, and verifying the file
  ASSERT_EQ(emu_close(fd), 0);
  fd = emu_open("::bigfile", O_RDWR, 0644);
  char readbuf[kBufSize];
  ssize_t bytes_read = 0;
  data = data_a;
  while (bytes_read < sz) {
    r = emu_read(fd, readbuf, sizeof(readbuf));
    ASSERT_EQ(r, std::min(sz - bytes_read, static_cast<ssize_t>(sizeof(readbuf))));
    ASSERT_EQ(memcmp(readbuf, data, r), 0);
    data = rotate(data);
    bytes_read += r;
  }

  ASSERT_EQ(bytes_read, sz);

  ASSERT_EQ(emu_close(fd), 0);
  ASSERT_EQ(RunFsck(), 0);
}

}  // namespace
}  // namespace fs_test
