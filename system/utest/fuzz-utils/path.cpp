// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fuzz-utils/path.h>
#include <unittest/unittest.h>
#include <zircon/syscalls.h>

namespace fuzzing {
namespace testing {
namespace {

bool TestJoin() {
    BEGIN_TEST;

    Path path;
    EXPECT_STR_EQ(path.c_str(), "/");

    path.Reset();
    fbl::String str = fbl::move(path.Join(""));
    EXPECT_STR_EQ(path.c_str(), "/");

    path.Reset();
    str = fbl::move(path.Join("tmp"));
    EXPECT_STR_EQ(str.c_str(), "/tmp");

    str = fbl::move(path.Join("/foo"));
    EXPECT_STR_EQ(str.c_str(), "/foo");

    str = fbl::move(path.Join("bar/"));
    EXPECT_STR_EQ(str.c_str(), "/bar");

    str = fbl::move(path.Join("//baz//"));
    EXPECT_STR_EQ(str.c_str(), "/baz");

    path.Reset();
    str = fbl::move(path.Join("tmp//foo//bar//baz"));
    EXPECT_STR_EQ(str.c_str(), "/tmp/foo/bar/baz");

    END_TEST;
}

BEGIN_TEST_CASE(PathTest)
RUN_TEST(TestJoin)
END_TEST_CASE(PathTest)

} // namespace
} // namespace testing
} // namespace fuzzing
