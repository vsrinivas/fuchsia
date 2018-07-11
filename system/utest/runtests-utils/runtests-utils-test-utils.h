// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

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


namespace runtests {

static constexpr char kExpectedJSONOutputPrefix[] = "{\"tests\":[\n";
// We don't want to count the null terminator.
static constexpr size_t kExpectedJSONOutputPrefixSize =
    sizeof(kExpectedJSONOutputPrefix) - 1;

// Creates a script file with given contents in its constructor and deletes it
// in its destructor.
class ScopedScriptFile {

public:
    // |path| is the path of the file to be created. Should start with
    // kMemFsPath. |contents| are the script contents. Shebang line will be
    // added automatically.
    ScopedScriptFile(const fbl::StringPiece path,
                     const fbl::StringPiece contents);
    ~ScopedScriptFile();
    fbl::StringPiece path() const;

private:
    const fbl::StringPiece path_;
};


// Creates a script file with given contents in its constructor and deletes it
// in its destructor.
class ScopedTestFile {

public:
    // |path| is the path of the file to be created. Should start with kMemFsPath.
    // |contents| are the script contents. Shebang line will be added automatically.
    ScopedTestFile(const fbl::StringPiece path, const fbl::StringPiece file);
    ~ScopedTestFile();
    fbl::StringPiece path() const;

private:
    const fbl::StringPiece path_;
};


// Creates a subdirectory of TestFsRoot() in its constructor and deletes it in
// its destructor.
class ScopedTestDir {

public:
    ScopedTestDir()
        : basename_(NextBasename()), path_(JoinPath(TestFsRoot(), basename_)) {
        if (mkdir(path_.c_str(), 0755)) {
            printf("FAILURE: mkdir failed to open %s: %s\n", path_.c_str(),
                   strerror(errno));
            exit(1);
        }
    }
    ~ScopedTestDir() { CleanUpDir(path_.c_str()); }
    const char* basename() { return basename_.c_str(); }
    const char* path() { return path_.c_str(); }

private:
    fbl::String NextBasename() {
        // More than big enough to print INT_MAX.
        char buf[64];
        sprintf(buf, "%d", num_test_dirs_created_++);
        return fbl::String(buf);
    }

    // Recursively removes the directory at |dir_path| and its contents.
    static void CleanUpDir(const char* dir_path) {
        struct dirent* entry;
        DIR* dp;

        dp = opendir(dir_path);
        if (dp == nullptr) {
            // File found; remove it.
            remove(dir_path);
            return;
        }

        while ((entry = readdir(dp))) {
            // Skip "." and "..".
            if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
                continue;
            }
            fbl::String sub_dir_name = JoinPath(dir_path, entry->d_name);
            CleanUpDir(sub_dir_name.c_str());
        }
        closedir(dp);

        // Directory is now empty: remove it.
        rmdir(dir_path);
    }

    const fbl::String basename_;
    const fbl::String path_;

    // Used to generate unique subdirectories of TestFsRoot().
    static int num_test_dirs_created_;
};


class TestStopwatch : public Stopwatch {
public:
    void Start() override { start_called_ = true; }
    int64_t DurationInMsecs() override {
        BEGIN_HELPER;
        EXPECT_TRUE(start_called_);
        END_HELPER;
        return 14u;
    }

private:
    bool start_called_ = false;
};


// Returns the number of files or subdirectories in a given directory.
int NumEntriesInDir(const char* dir_path);

// Returns true if and only if the contents of |file| match |expected|.
bool CompareFileContents(FILE* file, const char* expected);

// Computes the relative path within |output_dir| of the output file of the
// test at |test_path|, setting |output_file_rel_path| as its value if
// successful.
// Returns true iff successful.
bool GetOutputFileRelPath(const fbl::StringPiece& output_dir,
                          const fbl::StringPiece& test_path,
                          fbl::String* output_file_rel_path);

} // namespace runtests
