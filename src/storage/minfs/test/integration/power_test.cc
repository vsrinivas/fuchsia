// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/stat.h>

#include <functional>
#include <string>
#include <vector>

#include <zxtest/zxtest.h>

#include "minfs_fixtures.h"
#include "utils.h"

namespace {

class PowerTest : public MinfsTestWithFvm {
 public:
  PowerTest() : runner_(this) {}
  void RunWithFailures(std::function<void()> function) { runner_.Run(function); }

 protected:
  fs::PowerFailureRunner runner_;
};

void RunBasics(void) {
  ASSERT_TRUE(CreateDirectory("/alpha"));
  ASSERT_TRUE(CreateDirectory("/alpha/bravo"));
  ASSERT_TRUE(CreateDirectory("/alpha/bravo/charlie"));
  ASSERT_TRUE(CreateDirectory("/alpha/bravo/charlie/delta"));
  ASSERT_TRUE(CreateDirectory("/alpha/bravo/charlie/delta/echo"));
  fbl::unique_fd fd1 = CreateFile("/alpha/bravo/charlie/delta/echo/foxtrot");
  ASSERT_TRUE(fd1);
  fbl::unique_fd fd2 = OpenFile("/alpha/bravo/charlie/delta/echo/foxtrot");
  ASSERT_TRUE(fd2);
  ASSERT_EQ(write(fd1.get(), "Hello, World!\n", 14), 14);
  fd1.reset();
  fd2.reset();

  fd1 = CreateFile("/file.txt");
  ASSERT_TRUE(fd1);

  ASSERT_EQ(unlink(BuildPath("/file.txt").c_str()), 0);
  ASSERT_TRUE(CreateDirectory("/emptydir"));
  fd1 = OpenReadOnly("/emptydir");
  ASSERT_TRUE(fd1);

  ASSERT_EQ(rmdir(BuildPath("/emptydir").c_str()), 0);
}

TEST_F(PowerTest, Basics) { RunWithFailures(&RunBasics); }

constexpr int kDataSize = 16 * 1024;
void* GetData() {
  static void* s_data_block = nullptr;
  if (!s_data_block) {
    s_data_block = new char[kDataSize];
    memset(s_data_block, 'y', kDataSize);
  }
  return s_data_block;
}

fbl::unique_fd CreateAndWrite(const std::string& path) {
  fbl::unique_fd file = CreateFile(path);
  write(file.get(), GetData(), kDataSize);
  return file;
}

void RunDeleteAndWrite(void) {
  CreateDirectory("/alpha");

  std::vector<std::string> names = {
      BuildPath("/alpha/bravo"), BuildPath("/alpha/charlie"), BuildPath("/alpha/delta"),
      BuildPath("/alpha/echo"),  BuildPath("/alpha/foxtrot"),
  };
  std::vector<fbl::unique_fd> files;

  for (size_t i = 0; i < 10; i++) {
    files.push_back(CreateAndWrite(names[i % names.size()]));
    ASSERT_TRUE(files[i]);
    if (i % 2) {
      // Create a directory over a file.
      unlink(names[i % names.size()].c_str());
      CreateDirectory(names[i % names.size()]);
    }

    if (i < 2) {
      continue;
    }

    // May or may nor succeed.
    write(files[i - 1].get(), GetData(), kDataSize);
    files[i - 2].reset();
    unlink(names[(i - 2) % names.size()].c_str());
  }
}

TEST_F(PowerTest, DeleteAndWrite) { RunWithFailures(&RunDeleteAndWrite); }

void RunLargeWrite(void) {
  for (int n = 0; n < 10; n++) {
    fbl::unique_fd file = CreateFile("the name");
    const int kBufferSize = 1024 * 1024 * 4;
    std::vector<char> buffer(kBufferSize);
    memset(buffer.data(), 'p', kBufferSize);
    write(file.get(), buffer.data(), kBufferSize);
    unlink("the name");
  }
}

TEST_F(PowerTest, LargeWrite) { RunWithFailures(&RunLargeWrite); }

}  // namespace
