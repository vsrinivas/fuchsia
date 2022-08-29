// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>

#include <gtest/gtest.h>

constexpr size_t MMAP_FILE_SIZE = 64;
constexpr intptr_t LIMIT_4GB = 0x80000000;

namespace {

TEST(MmapTest, Map32Test) {
  char* tmp = getenv("TEST_TMPDIR");
  std::string path = tmp == nullptr ? "/tmp/mmaptest" : std::string(tmp) + "/mmaptest";
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0777);
  ASSERT_GE(fd, 0);
  for (unsigned char i = 0; i < MMAP_FILE_SIZE; i++) {
    ASSERT_EQ(write(fd, &i, sizeof(i)), 1);
  }
  close(fd);

  int fdm = open(path.c_str(), O_RDWR);
  ASSERT_GE(fdm, 0);

  void* mapped =
      mmap(nullptr, MMAP_FILE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_32BIT, fdm, 0);
  intptr_t maploc = reinterpret_cast<intptr_t>(mapped);
  intptr_t limit = LIMIT_4GB - MMAP_FILE_SIZE;
  ASSERT_GT(maploc, 0);
  ASSERT_LE(maploc, limit);

  ASSERT_EQ(munmap(mapped, MMAP_FILE_SIZE), 0);
  close(fd);

  unlink(path.c_str());
}

}  // namespace
