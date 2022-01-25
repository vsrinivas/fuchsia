// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/test_filesystem.h"

namespace fs_test {
namespace {

using FullTestParamType = std::tuple<TestFilesystemOptions, bool>;

class FullTest : public BaseFilesystemTest, public testing::WithParamInterface<FullTestParamType> {
 public:
  FullTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  bool should_remount() const { return std::get<1>(GetParam()); }
};

void FillFile(fbl::unique_fd fd, unsigned char value) {
  unsigned char buf[4096] = {value};
  for (;;) {
    ssize_t written = write(fd.get(), buf, sizeof(buf));
    if (written < ssize_t{sizeof(buf)}) {
      break;
    }
  }
  ASSERT_EQ(close(fd.release()), 0);
}

std::string GetDescriptionForFullTestParamType(
    const testing::TestParamInfo<FullTestParamType> param) {
  std::stringstream s;
  s << std::get<0>(param.param) << (std::get<1>(param.param) ? "WithRemount" : "WithoutRemount");
  return s.str();
}

void FillDirectoryEntries(FullTest& test) {
  for (int i = 0;; ++i) {
    std::string name = std::string("file-") + std::to_string(i);
    fbl::unique_fd fd(open(test.GetPath(name).c_str(), O_CREAT, 0644));
    if (!fd.is_valid()) {
      break;
    }
  }
}

void Remount(TestFilesystem& fs) {
  ASSERT_EQ(fs.Unmount().status_value(), ZX_OK);
  ASSERT_EQ(fs.Fsck().status_value(), ZX_OK);
  ASSERT_EQ(fs.Mount().status_value(), ZX_OK);
}

TEST_P(FullTest, ReadWhileFull) {
  {
    fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
    FillFile(std::move(fd), 0xFFu);
    FillDirectoryEntries(*this);
    if (should_remount()) {
      Remount(fs());
    }
  }

  // Can readdir...
  {
    DIR* dir = opendir(GetPath(".").c_str());
    ASSERT_NE(dir, nullptr);
    bool found_file = false;
    struct dirent* de;
    while ((de = readdir(dir)) != nullptr) {
      if (!strcmp(de->d_name, "file")) {
        found_file = true;
        break;
      }
    }
    ASSERT_TRUE(found_file);
    ASSERT_EQ(closedir(dir), 0);
  }

  // Can open...
  fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR, 0644));

  // Can stat...
  struct stat sb;
  ASSERT_EQ(fstat(fd.get(), &sb), 0);
  ASSERT_GT(sb.st_size, 0);

  // Can read...
  unsigned char buf = 0;
  ASSERT_EQ(read(fd.get(), &buf, sizeof(buf)), 1);
  ASSERT_EQ(buf, 0xFFu);
}

TEST_P(FullTest, CreateFileWhenFull) {
  {
    fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
    FillFile(std::move(fd), 0xFFu);
    FillDirectoryEntries(*this);
    if (should_remount()) {
      Remount(fs());
    }
  }

  // We want to try to create a file but we can't be certain it won't succeed (background cleanup
  // could have happened), so don't check the return value.
  fbl::unique_fd fd(open(GetPath("new-file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
}

TEST_P(FullTest, WriteToFileWhenFull) {
  {
    fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
    FillFile(std::move(fd), 0xFFu);
    FillDirectoryEntries(*this);
    if (should_remount()) {
      Remount(fs());
    }
  }

  // We want to try to write to the file but we can't be certain it won't succeed (background
  // cleanup could have happened), so don't check the return value.
  fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR, 0644));
  ASSERT_TRUE(fd.is_valid());
  unsigned char buf = 0;
  write(fd.get(), &buf, sizeof(buf));
}

TEST_P(FullTest, UnlinkWhenFullSucceeds) {
  {
    fbl::unique_fd fd(open(GetPath("file").c_str(), O_APPEND | O_RDWR | O_CREAT, 0644));
    FillFile(std::move(fd), 0xFFu);
    FillDirectoryEntries(*this);
    if (should_remount()) {
      Remount(fs());
    }
  }

  ASSERT_EQ(unlink(GetPath("file").c_str()), 0);
}

std::vector<FullTestParamType> GetTestParams() {
  std::vector<FullTestParamType> test_combinations;
  for (TestFilesystemOptions options : AllTestFilesystems()) {
    // Filesystems such as memfs cannot run this test because they OOM (as expected, given
    // memory is the limiting factor).
    if (options.filesystem->GetTraits().in_memory)
      continue;
    test_combinations.push_back(std::make_tuple(options, true));
    test_combinations.push_back(std::make_tuple(options, false));
  }
  return test_combinations;
}

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(FullTest);
INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, FullTest, testing::ValuesIn(GetTestParams()),
    GetDescriptionForFullTestParamType);

}  // namespace
}  // namespace fs_test
