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

#include "fixture.h"

namespace fuzzing {
namespace testing {
namespace {

// |fuzzing::testing::PathFixture| creates several empty files and directories for use in testing
// |fuzzing::Path|.
class PathFixture : public Fixture {
public:
    bool Create() override {
        BEGIN_HELPER;
        ASSERT_TRUE(Fixture::Create());
        ASSERT_TRUE(CreateFile("foo/ba/r", nullptr));
        ASSERT_TRUE(CreateFile("foo/ba/z/qu/x", "hello world"));
        ASSERT_TRUE(CreateDirectory("foo/ba/z/qu/ux"));
        END_HELPER;
    }
};

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

bool TestPushAndPop() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    EXPECT_STR_EQ(path.c_str(), "/");

    EXPECT_EQ(ZX_OK, path.Push("tmp"));
    EXPECT_STR_EQ(path.c_str(), "/tmp/");

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    EXPECT_EQ(ZX_OK, path.Push("//tmp"));
    EXPECT_STR_EQ(path.c_str(), "/tmp/");

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    EXPECT_EQ(ZX_OK, path.Push("tmp//"));
    EXPECT_STR_EQ(path.c_str(), "/tmp/");

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    EXPECT_EQ(ZX_OK, path.Push("//tmp//"));
    EXPECT_STR_EQ(path.c_str(), "/tmp/");

    EXPECT_NE(ZX_OK, path.Push(""));
    EXPECT_STR_EQ(path.c_str(), "/tmp/");

    EXPECT_NE(ZX_OK, path.Push("f"));

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    path.Reset();
    EXPECT_EQ(ZX_OK, path.Push(fixture.path().c_str()));
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    EXPECT_EQ(ZX_OK, path.Push("foo/ba"));
    EXPECT_STR_EQ(path.c_str(), fixture.path("foo/ba/").c_str());

    EXPECT_NE(ZX_OK, path.Push("r"));
    EXPECT_STR_EQ(path.c_str(), fixture.path("foo/ba/").c_str());

    EXPECT_EQ(ZX_OK, path.Push("z/qu/ux/"));
    EXPECT_STR_EQ(path.c_str(), fixture.path("foo/ba/z/qu/ux/").c_str());

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), fixture.path("foo/ba/").c_str());

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    END_TEST;
}

bool TestReset() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path().c_str()));

    path.Reset();
    EXPECT_STR_EQ(path.c_str(), "/");

    END_TEST;
}

BEGIN_TEST_CASE(PathTest)
RUN_TEST(TestJoin)
RUN_TEST(TestPushAndPop)
RUN_TEST(TestReset)
END_TEST_CASE(PathTest)

} // namespace
} // namespace testing
} // namespace fuzzing
