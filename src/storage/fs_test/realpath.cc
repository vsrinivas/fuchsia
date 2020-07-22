// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/unique_fd.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

using RealpathTest = FilesystemTest;

constexpr char kName[] = "my_file";
constexpr char kTestNameDotDot[] = "foo/../bar/../my_file";
constexpr char kTestNameDot[] = "././././my_file";
constexpr char kTestNameBothDots[] = "foo//.././/./././my_file";

bool terminator(char c) { return c == 0 || c == '/'; }

bool is_resolved(const char* path) {
  // Check that there are no ".", "//", or ".." components.
  // We assume there are no symlinks, since symlinks are not
  // yet supported on Fuchsia.
  while (true) {
    if (path[0] == 0) {
      return true;
    } else if (path[0] == '.' && terminator(path[1])) {
      return false;
    } else if (path[0] == '/' && path[1] == '/') {
      return false;
    } else if (path[0] == '.' && path[1] == '.' && terminator(path[2])) {
      return false;
    }
    if ((path = strchr(path, '/')) == nullptr) {
      return true;
    }
    path += 1;
  }
}

TEST_P(RealpathTest, Absolute) {
  fbl::unique_fd fd(open(GetPath(kName).c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);

  struct stat sb;
  ASSERT_EQ(stat(GetPath(kName).c_str(), &sb), 0);

  // Find the real path of the file.
  char buf[PATH_MAX];
  ASSERT_EQ(realpath(GetPath(kName).c_str(), buf), buf);

  // Confirm that for (resolvable) cases of realpath, the name
  // can be cleaned.
  char buf2[PATH_MAX];
  ASSERT_EQ(realpath(GetPath(kTestNameDotDot).c_str(), buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  ASSERT_EQ(realpath(GetPath(kTestNameDot).c_str(), buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  ASSERT_EQ(realpath(GetPath(kTestNameBothDots).c_str(), buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  // Clean up
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(GetPath(kName).c_str()), 0);
}

constexpr char kNameDir[] = "my_dir";
constexpr char kNameFile[] = "my_dir/my_file";
constexpr char kTestRelativeDotDot[] = "../my_dir/../my_dir/my_file";
constexpr char kTestRelativeDot[] = "./././my_file";
constexpr char kTestRelativeBothDots[] = "./..//my_dir/.././///././my_dir/./my_file";

TEST_P(RealpathTest, Relative) {
  ASSERT_EQ(mkdir(GetPath(kNameDir).c_str(), 0666), 0);
  fbl::unique_fd fd(open(GetPath(kNameFile).c_str(), O_RDWR | O_CREAT, 0644));
  ASSERT_TRUE(fd);
  close(fd.release());

  struct stat sb;
  ASSERT_EQ(stat(GetPath(kNameFile).c_str(), &sb), 0);

  // Find the real path of the file.
  char buf[PATH_MAX];
  ASSERT_EQ(realpath(GetPath(kNameFile).c_str(), buf), buf);

  char cwd[PATH_MAX];
  ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
  ASSERT_EQ(chdir(GetPath(kNameDir).c_str()), 0);

  char buf2[PATH_MAX];
  ASSERT_EQ(realpath(kTestRelativeDotDot, buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  ASSERT_EQ(realpath(kTestRelativeDot, buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  ASSERT_EQ(realpath(kTestRelativeBothDots, buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  // Test the longest possible path name

  // Extract the current working directory name ("my_dir/my_file" - "my_file")
  size_t cwd_len = strlen(buf) - strlen("my_file");
  char bufmax[PATH_MAX + 1];
  bufmax[0] = '.';
  size_t len = 1;
  // When realpath completes, it should return a result of the
  // form "CWD + '/' + "my_file".
  //
  // Ensure that our (uncanonicalized) path, including the CWD,
  // can fit within PATH_MAX (but just barely).
  while (len != PATH_MAX - cwd_len - strlen("my_file") - 1) {
    bufmax[len++] = '/';
  }
  strcpy(bufmax + len, "my_file");
  ASSERT_EQ(strlen(bufmax), PATH_MAX - cwd_len - 1);

  ASSERT_EQ(realpath(bufmax, buf2), buf2);
  ASSERT_EQ(std::string(buf), std::string(buf2));
  ASSERT_TRUE(is_resolved(buf2));

  // Try a name that is too long (same as the last one, but just
  // add a single additional "/").
  bufmax[len++] = '/';
  strcpy(bufmax + len, "my_file");
  ASSERT_EQ(realpath(bufmax, buf2), nullptr);

  // Clean up
  ASSERT_EQ(chdir(cwd), 0) << "Could not return to original cwd";
  ASSERT_EQ(unlink(GetPath(kNameFile).c_str()), 0);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, RealpathTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
