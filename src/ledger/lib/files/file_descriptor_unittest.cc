// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/files/file_descriptor.h"

#include <string>

#include "gtest/gtest.h"
#include "src/ledger/lib/files/eintr_wrapper.h"
#include "src/ledger/lib/files/file.h"
#include "src/ledger/lib/files/scoped_tmp_dir.h"
#include "src/ledger/lib/files/unique_fd.h"
#include "src/ledger/lib/logging/logging.h"

namespace ledger {
namespace {

TEST(FileDescriptor, WriteAndRead) {
  ScopedTmpDir temp_dir;
  unique_fd dirfd(openat(temp_dir.path().root_fd(), temp_dir.path().path().c_str(), O_RDONLY));
  ASSERT_TRUE(dirfd.is_valid());

  std::string filename = "bar";
  std::string content = "";
  EXPECT_TRUE(WriteFileAt(dirfd.get(), filename, content.c_str(), content.size()));

  unique_fd fd(openat(dirfd.get(), filename.c_str(), O_RDWR));
  // unique_fd fd(open(path.c_str(), O_RDWR));
  ASSERT_TRUE(fd.is_valid());

  std::string string = "one, two, three";
  EXPECT_TRUE(WriteFileDescriptor(fd.get(), string.data(), string.size()));
  EXPECT_EQ(0, lseek(fd.get(), 0, SEEK_SET));

  std::vector<char> buffer;
  buffer.resize(1024);
  ssize_t read = ReadFileDescriptor(fd.get(), buffer.data(), 1024);
  EXPECT_EQ(static_cast<ssize_t>(string.size()), read);
  EXPECT_EQ(string, buffer.data());
}

}  // namespace

}  // namespace ledger
