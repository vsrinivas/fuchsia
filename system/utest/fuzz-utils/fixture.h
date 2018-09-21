// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdarg.h>

#include <fbl/macros.h>
#include <fbl/string.h>
#include <unittest/unittest.h>

#define EXPECT_CSTR_EQ(x, y) EXPECT_STR_EQ((x).c_str(), (y).c_str())

namespace fuzzing {
namespace testing {

// |fuzzing::testing::Fixture| is a helper class to set up and tear down several paths.  All paths
// are under a randomly generated subdirectory of /tmp, both to minimize interference between
// parallel tests and reduce clutter left by a hard crash.
class Fixture {
public:
    virtual ~Fixture();

    // Combines optional, (v)printf-style relative path described by |fmt| with the current root
    // directory to give a full path.  If |fmt| is omitted, returns the testing root directory.
    fbl::String path(const char* fmt = nullptr, ...) const;
    fbl::String path(const char* fmt, va_list ap) const;

protected:
    Fixture();

    // Creates the temporary directory to act as the test root.
    virtual bool Create();

    // Create a file at |path|, first creating any parent directories needed.  Write the given
    // |contents| to the file, if provided.
    bool CreateFile(const char* path, const char* contents = nullptr);
    bool CreateFile(const fbl::String& path, const char* contents = nullptr) {
        return CreateFile(path.c_str(), contents);
    }

    // Creates a directory at |path|, creating any parent directories as needed.
    bool CreateDirectory(const char* path);
    bool CreateDirectory(const fbl::String& path) { return CreateDirectory(path.c_str()); }

    // Deletes a directory and all of its contents
    bool RemoveDirectory(const char* path);
    bool RemoveDirectory(const fbl::String& path) { return RemoveDirectory(path.c_str()); }

    // Removes the root directory if set and resets the object to a pristine state.
    virtual void Reset();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Fixture);

    // String used to hold the random temporary directory
    fbl::String root_;
    // String used to hold last result of |get_unsafe|.  See note on that method!!
    fbl::String unsafe_;
};

} // namespace testing
} // namespace fuzzing
