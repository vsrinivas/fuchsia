// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string>

#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>

#include "gtest/gtest.h"
#include "src/storage/fs_test/fs_test_fixture.h"
#include "src/storage/fs_test/misc.h"

namespace fs_test {
namespace {

using UnicodeTest = FilesystemTest;

// Character that is 32 bits when encoded in UTF-8.
// U+1F60E
constexpr char kSunglasses[] = "\xf0\x9f\x98\x8e";
// Character that is 24 bits when encoded in UTF-8.
// U+203D
constexpr char kInterrobang[] = "\xe2\x80\xbd";
// Character that is 16 bits when encoded in UTF-8.
// U+00F7
constexpr char kDivisionSign[] = "\xc3\xb7";
// Character that is 16 bits when encoded in UTF-8, but 8 bits when encoded in UTF-16.
// U+00BF
constexpr char kInvertedQuestionMark[] = "\xc2\xbf";

void TestUnicodeDirectoryHasCorrectName(UnicodeTest* test, const char* name) {
  ASSERT_EQ(mkdir(test->GetPath(name).c_str(), 0755), 0) << strerror(errno);

  DIR* dir = opendir(test->GetPath("").c_str());
  ASSERT_NE(dir, nullptr) << strerror(errno);

  struct dirent* de;
  bool seen = false;
  while ((de = readdir(dir)) != nullptr) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      // Ignore these entries
      continue;
    }
    ASSERT_EQ(strcmp(name, de->d_name), 0);
    seen = true;
  }
  closedir(dir);

  ASSERT_TRUE(seen) << "Did not find expected file " << name;

  ASSERT_EQ(rmdir(test->GetPath(name).c_str()), 0) << name << ": " << strerror(errno);
}

TEST_P(UnicodeTest, TestUnicodeDirectoryNames) {
  ASSERT_NO_FATAL_FAILURE(TestUnicodeDirectoryHasCorrectName(this, kSunglasses));
  ASSERT_NO_FATAL_FAILURE(TestUnicodeDirectoryHasCorrectName(this, kInterrobang));
  ASSERT_NO_FATAL_FAILURE(TestUnicodeDirectoryHasCorrectName(this, kDivisionSign));
  ASSERT_NO_FATAL_FAILURE(TestUnicodeDirectoryHasCorrectName(this, kInvertedQuestionMark));
}

TEST_P(UnicodeTest, TestRenameUnicodeSucceeds) {
  ASSERT_EQ(mkdir(GetPath(kSunglasses).c_str(), 0755), 0);
  // Note that on FAT32 this wouldn't change the short name of the directory.
  ASSERT_EQ(rename(GetPath(kSunglasses).c_str(), GetPath(kInterrobang).c_str()), 0)
      << strerror(errno);

  DIR* d;
  ASSERT_EQ(opendir(GetPath(kSunglasses).c_str()), nullptr);
  ASSERT_NE(d = opendir(GetPath(kInterrobang).c_str()), nullptr);
  closedir(d);

  // This would though - we go from having two UTF-16 codepoints to one.
  ASSERT_EQ(rename(GetPath(kInterrobang).c_str(), GetPath(kDivisionSign).c_str()), 0);

  ASSERT_EQ(opendir(GetPath(kInterrobang).c_str()), nullptr);
  ASSERT_NE(d = opendir(GetPath(kDivisionSign).c_str()), nullptr);
  closedir(d);
}

TEST_P(UnicodeTest, TestNonUtf8Names) {
  // Valid UTF-8 byte sequences follow these bit patterns:
  // 0xxx_xxxx
  // 110x_xxxx 10xx_xxxx
  // 1110_xxxx 10xx_xxxx 10xx_xxxx
  // 1111_0xxx 10xx_xxxx 10xx_xxxx 10xx_xxxx
  // This sequence is invalid because bit zero is set in the first byte, but bit one is not set
  // (it's 1000_0000 1000_0001).
  constexpr char kInvalidBytes[] = "\x80\x81";
  ASSERT_EQ(mkdir(GetPath(kInvalidBytes).c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
}

void TestCreateAndDeleteUnicodeFilename(UnicodeTest* test, const char* name) {
  int fd;

  ASSERT_GE(fd = open(test->GetPath(name).c_str(), O_RDWR | O_CREAT), 0) << strerror(errno);

  int result = write(fd, "abc", 4);
  ASSERT_GT(result, 0) << strerror(errno);

  ASSERT_EQ(unlink(test->GetPath(name).c_str()), 0) << strerror(errno);
  close(fd);
}

TEST_P(UnicodeTest, TestUnicodeFileNames) {
  ASSERT_NO_FATAL_FAILURE(TestCreateAndDeleteUnicodeFilename(this, kSunglasses));
  ASSERT_NO_FATAL_FAILURE(TestCreateAndDeleteUnicodeFilename(this, kInterrobang));
  ASSERT_NO_FATAL_FAILURE(TestCreateAndDeleteUnicodeFilename(this, kDivisionSign));
  ASSERT_NO_FATAL_FAILURE(TestCreateAndDeleteUnicodeFilename(this, kInvertedQuestionMark));
}

TEST_P(UnicodeTest, TestUtf16UnpairedSurrogate) {
  // This decodes to U+D800, which is reserved as a value for the first two bytes in a 4-byte UTF-16
  // character.
  constexpr char kUnpairedHighSurrogate[] = "\xed\xa0\x80";
  ASSERT_EQ(mkdir(GetPath(kUnpairedHighSurrogate).c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);

  // This is U+DC00, which must be the last two bytes in a 4-byte UTF-16 character.
  constexpr char kUnpairedLowSurrogate[] = "\xed\xb0\x80";
  ASSERT_EQ(mkdir(GetPath(kUnpairedLowSurrogate).c_str(), 0755), -1);
  ASSERT_EQ(errno, EINVAL);
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, UnicodeTest, testing::ValuesIn(AllTestFilesystems()),
                         testing::PrintToStringParamName());

}  // namespace
}  // namespace fs_test
