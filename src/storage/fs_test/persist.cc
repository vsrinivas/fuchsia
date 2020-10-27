// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zircon/syscalls.h>

#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <fbl/alloc_checker.h>
#include <fbl/string_piece.h>
#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using PersistTest = FilesystemTest;

constexpr bool IsDirectory(const char* const path) {
  return path[fbl::constexpr_strlen(path) - 1] == '/';
}

TEST_P(PersistTest, Simple) {
  const char* const relative_paths[] = {
      "abc", "def/", "def/def_subdir/", "def/def_subdir/def_subfile",
      "ghi", "jkl",  "mnopqrstuvxyz"};
  std::string paths[std::size(relative_paths)];
  for (size_t i = 0; i < std::size(paths); i++) {
    paths[i] = GetPath(relative_paths[i]);
    if (IsDirectory(paths[i].c_str())) {
      ASSERT_EQ(mkdir(paths[i].c_str(), 0644), 0);
    } else {
      fbl::unique_fd fd(open(paths[i].c_str(), O_RDWR | O_CREAT | O_EXCL, 0644));
      ASSERT_TRUE(fd);
    }
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // The files should still exist when we remount
  for (ssize_t i = std::size(paths) - 1; i >= 0; i--) {
    if (IsDirectory(paths[i].c_str())) {
      ASSERT_EQ(rmdir(paths[i].c_str()), 0);
    } else {
      ASSERT_EQ(unlink(paths[i].c_str()), 0);
    }
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // But they should stay deleted!
  for (ssize_t i = std::size(paths) - 1; i >= 0; i--) {
    if (IsDirectory(paths[i].c_str())) {
      ASSERT_EQ(rmdir(paths[i].c_str()), -1);
    } else {
      ASSERT_EQ(unlink(paths[i].c_str()), -1);
    }
  }
}

TEST_P(PersistTest, RapidRemount) {
  for (size_t i = 0; i < 10; i++) {
    ASSERT_EQ(fs().Unmount().status_value(), ZX_OK);
    ASSERT_EQ(fs().Fsck().status_value(), ZX_OK);
    ASSERT_EQ(fs().Mount().status_value(), ZX_OK);
  }
}

using WithBufferSize = std::tuple<TestFilesystemOptions, /*buffer_size=*/size_t>;

class PersistWithDataTest : public BaseFilesystemTest,
                            public testing::WithParamInterface<WithBufferSize> {
 public:
  PersistWithDataTest() : BaseFilesystemTest(std::get<0>(GetParam())) {}

  size_t buffer_size() const { return std::get<1>(GetParam()); }
};

TEST_P(PersistWithDataTest, ReadsReturnWrittenDataAfterRemount) {
  const std::string files[] = {
      GetPath("abc"),
      GetPath("def"),
      GetPath("and-another-file-filled-with-data"),
  };
  std::unique_ptr<uint8_t[]> buffers[std::size(files)];
  unsigned int seed = static_cast<unsigned int>(zx_ticks_get());
  std::cout << "Persistent data test using seed: " << seed << std::endl;
  fbl::AllocChecker ac;
  for (size_t i = 0; i < std::size(files); i++) {
    buffers[i].reset(new (&ac) uint8_t[buffer_size()]);
    ASSERT_TRUE(ac.check());

    for (size_t j = 0; j < buffer_size(); j++) {
      buffers[i][j] = (uint8_t)rand_r(&seed);
    }
    fbl::unique_fd fd(open(files[i].c_str(), O_RDWR | O_CREAT, 0644));
    ASSERT_TRUE(fd);
    ASSERT_EQ(write(fd.get(), &buffers[i][0], buffer_size()), static_cast<ssize_t>(buffer_size()));
    ASSERT_EQ(fsync(fd.get()), 0);
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Read files
  for (size_t i = 0; i < std::size(files); i++) {
    std::unique_ptr<uint8_t[]> rbuf(new (&ac) uint8_t[buffer_size()]);
    ASSERT_TRUE(ac.check());
    fbl::unique_fd fd(open(files[i].c_str(), O_RDONLY, 0644));
    ASSERT_TRUE(fd);

    struct stat buf;
    ASSERT_EQ(fstat(fd.get(), &buf), 0);
    ASSERT_EQ(buf.st_nlink, 1ul);
    ASSERT_EQ(buf.st_size, static_cast<off_t>(buffer_size()));

    ASSERT_EQ(read(fd.get(), &rbuf[0], buffer_size()), static_cast<ssize_t>(buffer_size()));
    for (size_t j = 0; j < buffer_size(); j++) {
      ASSERT_EQ(rbuf[j], buffers[i][j]);
    }
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Delete all files
  for (size_t i = 0; i < std::size(files); i++) {
    ASSERT_EQ(unlink(files[i].c_str()), 0);
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Files should stay deleted

  DIR* dirp = opendir(GetPath("").c_str());
  ASSERT_NE(dirp, nullptr);
  struct dirent* de;
  de = readdir(dirp);
  ASSERT_NE(de, nullptr);
  ASSERT_EQ(strncmp(de->d_name, ".", 1), 0);
  ASSERT_EQ(readdir(dirp), nullptr);
  ASSERT_EQ(closedir(dirp), 0);
}

using PersistRenameLoopTestParam = std::tuple<TestFilesystemOptions, /*directory=*/bool,
                                              std::tuple</*loop_length=*/int, /*moves=*/int>>;

const TestFilesystemOptions& FilesystemOptions(const PersistRenameLoopTestParam& param) {
  return std::get<0>(param);
}

bool IsDirectory(const PersistRenameLoopTestParam& param) { return std::get<1>(param); }

int LoopLength(const PersistRenameLoopTestParam& param) { return std::get<0>(std::get<2>(param)); }

int Moves(const PersistRenameLoopTestParam& param) { return std::get<1>(std::get<2>(param)); }

class PersistRenameLoopTest : public BaseFilesystemTest,
                              public testing::WithParamInterface<PersistRenameLoopTestParam> {
 public:
  PersistRenameLoopTest() : BaseFilesystemTest(FilesystemOptions(GetParam())) {}
};

TEST_P(PersistRenameLoopTest, MultipleRenamesCorrectAfterRemount) {
  // Create loop_length() directories
  for (int i = 0; i < LoopLength(GetParam()); i++) {
    const std::string src = GetPath(std::string(1, 'a' + i));
    ASSERT_EQ(mkdir(src.c_str(), 0644), 0);
  }

  const std::string target_name = "target";
  std::string src = "a/" + target_name;
  // Create a 'target'
  if (IsDirectory(GetParam())) {
    ASSERT_EQ(mkdir(GetPath(src).c_str(), 0644), 0);
  } else {
    fbl::unique_fd fd(open(GetPath(src).c_str(), O_RDWR | O_CREAT));
    ASSERT_TRUE(fd);
  }

  // Move the target through the loop a bunch of times
  int to_do = Moves(GetParam());
  size_t char_index = 0;
  std::string dst = src;
  while (to_do--) {
    char_index = (char_index + 1) % LoopLength(GetParam());
    dst[0] = 'a' + char_index;
    ASSERT_EQ(rename(GetPath(src).c_str(), GetPath(dst).c_str()), 0);
    src[0] = 'a' + char_index;
  }

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Check that the target only exists in ONE directory
  bool target_found = false;
  for (int i = 0; i < LoopLength(GetParam()); i++) {
    std::string src(1, 'a' + i);
    DIR* dirp = opendir(GetPath(src).c_str());
    ASSERT_NE(dirp, nullptr);
    struct dirent* de;
    de = readdir(dirp);
    ASSERT_NE(de, nullptr);
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    de = readdir(dirp);
    if (de != nullptr) {
      ASSERT_FALSE(target_found) << "Target found twice!";
      ASSERT_EQ(de->d_name, target_name) << "Non-target found";
      target_found = true;
    }

    ASSERT_EQ(closedir(dirp), 0);
  }
  ASSERT_TRUE(target_found);

  EXPECT_EQ(fs().Unmount().status_value(), ZX_OK);
  EXPECT_EQ(fs().Fsck().status_value(), ZX_OK);
  EXPECT_EQ(fs().Mount().status_value(), ZX_OK);

  // Clean up

  target_found = false;
  for (int i = 0; i < LoopLength(GetParam()); i++) {
    std::string src(1, 'a' + i);
    int ret = unlink(GetPath(src).c_str());
    if (ret != 0) {
      ASSERT_FALSE(target_found);
      ASSERT_EQ(unlink(GetPath(src + "/" + target_name).c_str()), 0);
      ASSERT_EQ(unlink(GetPath(src).c_str()), 0);
      target_found = true;
    }
  }
  ASSERT_TRUE(target_found) << "Target was never unlinked";
}

std::vector<TestFilesystemOptions> GetTestCombinations() {
  return MapAndFilterAllTestFilesystems(
      [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
        // These tests only work on filesystems that can be unmounted.
        if (options.filesystem->GetTraits().can_unmount) {
          return options;
        } else {
          return std::nullopt;
        }
      });
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, PersistTest, testing::ValuesIn(GetTestCombinations()),
                         testing::PrintToStringParamName());

std::string PersistWithDataTestParamDescription(
    const testing::TestParamInfo<WithBufferSize>& param) {
  std::stringstream s;
  s << std::get<0>(param.param) << "WithBufferSize" << std::get<1>(param.param);
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, PersistWithDataTest,
                         testing::Combine(testing::ValuesIn(GetTestCombinations()),
                                          testing::Values(1, 100, 8192 - 1, 8192, 8192 + 1,
                                                          8192 * 128)),
                         PersistWithDataTestParamDescription);

std::string PersistRenameLoopTestParamDescription(
    const testing::TestParamInfo<PersistRenameLoopTestParam>& param) {
  std::stringstream s;
  s << FilesystemOptions(param.param)
    << (IsDirectory(param.param) ? "RenameDirectory" : "RenameFile") << Moves(param.param)
    << "Times"
    << "Through" << LoopLength(param.param) << "Directories";
  return s.str();
}

INSTANTIATE_TEST_SUITE_P(
    /*no prefix*/, PersistRenameLoopTest,
    testing::Combine(testing::ValuesIn(GetTestCombinations()), testing::Bool(),
                     testing::Values(std::make_tuple(2, 2), std::make_tuple(2, 100),
                                     std::make_tuple(15, 100), std::make_tuple(25, 500))),
    PersistRenameLoopTestParamDescription);

}  // namespace
}  // namespace fs_test
