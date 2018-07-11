// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/unique_fd.h>
#include <fbl/unique_ptr.h>
#include <fbl/vector.h>
#include <runtests-utils/runtests-utils.h>
#include <unittest/unittest.h>

#include "runtests-utils-test-globals.h"
#include "runtests-utils-test-utils.h"


namespace runtests {

///////////////////////////////////////////////////////////////////////////////
// HELPER CLASSES
///////////////////////////////////////////////////////////////////////////////

ScopedScriptFile::ScopedScriptFile(const fbl::StringPiece path,
                                   const fbl::StringPiece contents)
    : path_(path) {
    const int fd = open(path_.data(), O_CREAT | O_WRONLY, S_IRWXU);
    ZX_ASSERT_MSG(-1 != fd, "%s", strerror(errno));
    ZX_ASSERT(
        sizeof(kScriptShebang) ==
        static_cast<size_t>(write(fd, kScriptShebang, sizeof(kScriptShebang))));
    ZX_ASSERT(contents.size() ==
              static_cast<size_t>(write(fd, contents.data(), contents.size())));
    ZX_ASSERT_MSG(-1 != close(fd), "%s", strerror(errno));
}

ScopedScriptFile::~ScopedScriptFile() {
    remove(path_.data());
}

fbl::StringPiece ScopedScriptFile::path() const {
    return path_;
}

ScopedTestFile::ScopedTestFile(
    const fbl::StringPiece path, const fbl::StringPiece file)
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

ScopedTestFile::~ScopedTestFile() {
    remove(path_.data());
}

fbl::StringPiece ScopedTestFile::path() const {
    return path_;
}

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

// Returns true if and only if the contents of |file| match |expected|.
bool CompareFileContents(FILE* file, const char* expected) {
    BEGIN_HELPER;
    // Get the size of the file contents, copy it into a buffer, and compare.
    ASSERT_EQ(0, fseek(file, 0, SEEK_END));
    const long unsigned int size = ftell(file);
    rewind(file);
    fbl::unique_ptr<char[]> buf(new char[size + 1]);
    buf[size] = 0;
    ASSERT_EQ(size, fread(buf.get(), sizeof(char), size, file));
    EXPECT_STR_EQ(expected, buf.get());
    END_HELPER;
}

// Computes the relative path within |output_dir| of the output file of the
// test at |test_path|, setting |output_file_rel_path| as its value if
// successful.
// Returns true iff successful.
bool GetOutputFileRelPath(const fbl::StringPiece& output_dir,
                          const fbl::StringPiece& test_path,
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
        printf("FAILURE: there are %d entries in %s. There should only be a "
               "single output file\n",
               num_entries, dir_of_test_output.c_str());
    }
    return num_entries == 1;
}

namespace {

// This ensures that ScopedTestDir and ScopedScriptFile, which we make heavy
// use of in these tests, are indeed scoped and tear down without error.
bool ScopedDirsAndFilesAreIndeedScoped() {
    BEGIN_TEST;

    // Entering a test case, test_dir.path() should be empty.
    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    {
        ScopedTestDir dir;
        EXPECT_EQ(1, NumEntriesInDir(TestFsRoot()));
        EXPECT_EQ(0, NumEntriesInDir(dir.path()));
        {
            fbl::String file_name1 = JoinPath(dir.path(), "a.sh");
            ScopedScriptFile file1(file_name1, "A");
            EXPECT_EQ(1, NumEntriesInDir(dir.path()));
            {
                fbl::String file_name2 = JoinPath(dir.path(), "b.sh");
                ScopedScriptFile file2(file_name2, "B");
                EXPECT_EQ(2, NumEntriesInDir(dir.path()));
            }
            EXPECT_EQ(1, NumEntriesInDir(dir.path()));
        }
        EXPECT_EQ(0, NumEntriesInDir(dir.path()));
    }

    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    {
        ScopedTestDir dir1;
        ScopedTestDir dir2;
        ScopedTestDir dir3;
        EXPECT_EQ(3, NumEntriesInDir(TestFsRoot()));
    }

    EXPECT_EQ(0, NumEntriesInDir(TestFsRoot()));

    END_TEST;
}

BEGIN_TEST_CASE(TestHelpers)
RUN_TEST(ScopedDirsAndFilesAreIndeedScoped)
END_TEST_CASE(TestHelpers)

} // namespace
} // namespace runtests
