// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fixture.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/string.h>
#include <fbl/string_buffer.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace fuzzing {
namespace testing {

// Public methods

Fixture::~Fixture() {
    Reset();
}

fbl::String Fixture::path(const char* fmt, ...) const {
    va_list ap;
    va_start(ap, fmt);
    fbl::String result = path(fmt, ap);
    va_end(ap);
    return result;
}

fbl::String Fixture::path(const char* fmt, va_list ap) const {
    if (!fmt) {
        return root_;
    }

    fbl::StringBuffer<PATH_MAX> buffer;
    buffer.AppendVPrintf(fmt, ap);
    fbl::String relpath = buffer.ToString();
    if (relpath[0] == '/') {
        return relpath;
    }

    buffer.Clear();
    buffer.Append(root_);
    buffer.Append(relpath);

    return buffer.ToString();
}

// Protected methods

Fixture::Fixture() {}

bool Fixture::Create() {
    BEGIN_HELPER;
    Reset();

    uint64_t randnum;
    zx_cprng_draw(&randnum, sizeof(randnum));
    root_ = fbl::StringPrintf("/tmp/path-unit-test-%" PRIu64 "/", randnum);
    ASSERT_TRUE(CreateDirectory(nullptr));

    END_HELPER;
}

bool Fixture::CreateFile(const char* pathname, const char* contents) {
    BEGIN_HELPER;
    ASSERT_NONNULL(pathname);

    fbl::String local = path(pathname);
    pathname = local.c_str();

    const char* sep = strrchr(pathname, '/');
    fbl::String basename(pathname, sep - pathname);
    ASSERT_TRUE(CreateDirectory(basename));

    fbl::unique_fd fd(open(pathname, O_RDWR | O_CREAT, 0777));
    ASSERT_TRUE(!!fd);
    if (contents) {
        ASSERT_GE(write(fd.get(), contents, strlen(contents) + 1), 0);
    }

    END_HELPER;
}

bool Fixture::CreateDirectory(const char* pathname) {
    BEGIN_HELPER;

    fbl::String local = path(pathname);
    pathname = local.c_str();

    struct stat buf;
    if (stat(pathname, &buf) == 0) {
        ASSERT_TRUE(S_ISDIR(buf.st_mode));
    } else {
        ASSERT_EQ(errno, ENOENT);
        // Trim trailing slashes
        size_t len = strlen(pathname);
        while (len > 0 && pathname[len - 1] == '/') {
            --len;
        }
        local.Set(pathname, len);
        pathname = local.c_str();

        // Find last segment
        const char* sep = strrchr(pathname, '/');
        ASSERT_NONNULL(sep);
        fbl::String basename(pathname, sep - pathname);

        ASSERT_TRUE(CreateDirectory(basename));
        ASSERT_GE(mkdir(pathname, 0777), 0);
    }

    END_HELPER;
}

bool Fixture::RemoveDirectory(const char* pathname) {
    BEGIN_HELPER;
    ASSERT_NONNULL(pathname);

    char buffer[PATH_MAX];
    DIR* dir = opendir(pathname);
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir))) {
            if (strcmp(".", ent->d_name) == 0) {
                continue;
            }
            snprintf(buffer, sizeof(buffer), "%s/%s", pathname, ent->d_name);
            if (ent->d_type == DT_DIR) {
                EXPECT_TRUE(RemoveDirectory(buffer));
            } else {
                EXPECT_GE(unlink(buffer), 0);
            }
        }
        closedir(dir);
        EXPECT_GE(rmdir(pathname), 0);
    }

    END_HELPER;
}

void Fixture::Reset() {
    if (!root_.empty()) {
        RemoveDirectory(root_);
    }
    root_.clear();
    unsafe_.clear();
}

} // namespace testing
} // namespace fuzzing
