// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtests-utils-test-utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string_view>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>
#include <zxtest/zxtest.h>

namespace runtests {

///////////////////////////////////////////////////////////////////////////////
// HELPER CLASSES
///////////////////////////////////////////////////////////////////////////////

fbl::String packaged_script_dir() {
  fbl::String test_root_dir(getenv("TEST_ROOT_DIR"));
  fbl::String runtests_utils_test_data_dir(
      JoinPath(test_root_dir, "test/sys/runtests-utils-testdata"));
  return runtests_utils_test_data_dir;
}

PackagedScriptFile::PackagedScriptFile(const std::string_view path) {
  fbl::String script_dir = packaged_script_dir();
  path_ = JoinPath(script_dir, path);

  // Open the file to be sure that it exists.
  const int fd = open(path_.data(), O_RDONLY);
  ZX_ASSERT_MSG(-1 != fd, "%s", strerror(errno));
  ZX_ASSERT_MSG(-1 != close(fd), "%s", strerror(errno));
}

PackagedScriptFile::~PackagedScriptFile() {}

std::string_view PackagedScriptFile::path() const { return path_; }

ScopedStubFile::ScopedStubFile(const std::string_view path) : path_(path) {
  const int fd = open(path_.data(), O_CREAT | O_WRONLY, S_IRWXU);
  ZX_ASSERT_MSG(-1 != fd, "%s", strerror(errno));
  ZX_ASSERT_MSG(-1 != close(fd), "%s", strerror(errno));
}

ScopedStubFile::~ScopedStubFile() { remove(path_.data()); }

ScopedTestFile::ScopedTestFile(const std::string_view path, const std::string_view file)
    : path_(path) {
  fbl::unique_fd input_fd{open(file.data(), O_RDONLY)};
  ZX_ASSERT_MSG(input_fd, "%s", strerror(errno));

  fbl::unique_fd output_fd{open(path_.data(), O_CREAT | O_WRONLY, S_IRWXU)};
  ZX_ASSERT_MSG(output_fd, "%s", strerror(errno));

  constexpr size_t kBufSize = 1024;

  char buf[kBufSize];
  ssize_t n;
  while ((n = read(input_fd.get(), buf, kBufSize)) > 0) {
    ZX_ASSERT_MSG(write(output_fd.get(), buf, n) == n, "write failed: %s", strerror(errno));
  }
  ZX_ASSERT_MSG(n != -1, "read failed: %s", strerror(errno));
}

ScopedTestFile::~ScopedTestFile() { remove(path_.data()); }

std::string_view ScopedTestFile::path() const { return path_; }

int ScopedTestDir::num_test_dirs_created_ = 0;

///////////////////////////////////////////////////////////////////////////////
// FILE I/O HELPERS
///////////////////////////////////////////////////////////////////////////////

// Returns the number of files or subdirectories in a given directory.
int NumEntriesInDir(const char* dir_path) {
  struct dirent* entry;
  int num_entries = 0;
  DIR* dp;

  if (!(dp = opendir(dir_path))) {
    // dir_path actually points to a file. Return -1 by convention.
    return -1;
  }
  while ((entry = readdir(dp))) {
    // Skip "." and "..".
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    ++num_entries;
  }
  closedir(dp);
  return num_entries;
}

// Computes the relative path within |output_dir| of the output file of the
// test at |test_path|, setting |output_file_rel_path| as its value if
// successful.
// Returns true iff successful.
bool GetOutputFileRelPath(std::string_view output_dir, std::string_view test_path,
                          fbl::String* output_file_rel_path) {
  if (output_file_rel_path == nullptr) {
    printf("FAILURE: |output_file_rel_path| was null.");
    return false;
  }
  fbl::String dir_of_test_output = JoinPath(output_dir, test_path);
  DIR* dp = opendir(dir_of_test_output.c_str());
  if (dp == nullptr) {
    printf("FAILURE: could not open directory: %s\n", dir_of_test_output.c_str());
    return false;
  }
  struct dirent* entry;
  int num_entries = 0;
  fbl::String output_file_name;
  while ((entry = readdir(dp))) {
    // Skip "." and "..".
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
      continue;
    }
    if (entry->d_type != DT_REG) {
      continue;
    }
    output_file_name = fbl::String(entry->d_name);
    ++num_entries;
  }
  closedir(dp);
  *output_file_rel_path = JoinPath(test_path, output_file_name);
  if (num_entries != 1) {
    printf(
        "FAILURE: there are %d entries in %s. There should only be a "
        "single output file\n",
        num_entries, dir_of_test_output.c_str());
  }
  return num_entries == 1;
}

namespace {

// This ensures that ScopedTestDir, ScopedTestFile, and ScopedStubFile, which we
// make heavy use of in these tests, are indeed scoped and tear down without
// error.
TEST(TestHelpers, ScopedDirsAndFilesAreIndeedScoped) {
  // Entering a test case, test_dir.path() should be empty.
  EXPECT_EQ(0, NumEntriesInDir(kMemFsRoot));

  {
    ScopedTestDir dir;
    EXPECT_EQ(1, NumEntriesInDir(kMemFsRoot));
    EXPECT_EQ(0, NumEntriesInDir(dir.path()));
    {
      fbl::String file_name1 = JoinPath(dir.path(), "a.sh");
      PackagedScriptFile source_file_1("succeed.sh");
      ScopedTestFile file1(file_name1, source_file_1.path());

      EXPECT_EQ(1, NumEntriesInDir(dir.path()));
      {
        fbl::String file_name2 = JoinPath(dir.path(), "b.sh");
        ScopedStubFile file2(file_name2);
        EXPECT_EQ(2, NumEntriesInDir(dir.path()));
      }
      EXPECT_EQ(1, NumEntriesInDir(dir.path()));
    }
    EXPECT_EQ(0, NumEntriesInDir(dir.path()));
  }

  EXPECT_EQ(0, NumEntriesInDir(kMemFsRoot));

  {
    ScopedTestDir dir1;
    ScopedTestDir dir2;
    ScopedTestDir dir3;
    EXPECT_EQ(3, NumEntriesInDir(kMemFsRoot));
  }

  EXPECT_EQ(0, NumEntriesInDir(kMemFsRoot));
}

}  // namespace
}  // namespace runtests
