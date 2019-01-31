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
    fbl::String str = path.Join("");
    EXPECT_STR_EQ(path.c_str(), "/");

    path.Reset();
    str = path.Join("tmp");
    EXPECT_STR_EQ(str.c_str(), "/tmp");

    str = path.Join("/foo");
    EXPECT_STR_EQ(str.c_str(), "/foo");

    str = path.Join("bar/");
    EXPECT_STR_EQ(str.c_str(), "/bar");

    str = path.Join("//baz//");
    EXPECT_STR_EQ(str.c_str(), "/baz");

    path.Reset();
    str = path.Join("tmp//foo//bar//baz");
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
    EXPECT_EQ(ZX_OK, path.Push(fixture.path()));
    EXPECT_CSTR_EQ(path, fixture.path());

    EXPECT_EQ(ZX_OK, path.Push("foo/ba"));
    EXPECT_CSTR_EQ(path, fixture.path("foo/ba/"));

    EXPECT_NE(ZX_OK, path.Push("r"));
    EXPECT_CSTR_EQ(path, fixture.path("foo/ba/"));

    EXPECT_EQ(ZX_OK, path.Push("z/qu/ux/"));
    EXPECT_CSTR_EQ(path, fixture.path("foo/ba/z/qu/ux/"));

    path.Pop();
    EXPECT_CSTR_EQ(path, fixture.path("foo/ba/"));

    path.Pop();
    EXPECT_CSTR_EQ(path, fixture.path());

    path.Pop();
    EXPECT_STR_EQ(path.c_str(), "/");

    END_TEST;
}

bool TestGetSizeAndExists() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path("foo/ba/")));

    size_t size;
    EXPECT_EQ(ZX_OK, path.GetSize("r", &size));
    EXPECT_EQ(size, 0);

    // Non-existent and not file
    EXPECT_NE(ZX_OK, path.GetSize("q", &size));
    EXPECT_NE(ZX_OK, path.GetSize("z", &size));

    // +1 is for null terminator
    EXPECT_EQ(ZX_OK, path.GetSize("z/qu/x", &size));
    EXPECT_EQ(size, static_cast<size_t>(strlen("hello world") + 1));

    EXPECT_TRUE(path.IsFile("r"));
    EXPECT_FALSE(path.IsFile("q"));
    EXPECT_FALSE(path.IsFile("z"));
    EXPECT_TRUE(path.IsFile("z/qu/x"));

    END_TEST;
}

bool TestList() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path("foo")));

    fbl::unique_ptr<StringList> list;
    list = path.List();
    EXPECT_STR_EQ(list->first(), "ba");
    EXPECT_NULL(list->next());

    ASSERT_EQ(ZX_OK, path.Push("ba"));
    list = path.List();

    EXPECT_EQ(list->length(), 2);
    list->erase_if("r");
    list->erase_if("z");
    EXPECT_TRUE(list->is_empty());

    ASSERT_EQ(ZX_OK, path.Push("z/qu/ux"));
    list = path.List();
    EXPECT_TRUE(list->is_empty());

    END_TEST;
}

bool TestEnsureAndRemove() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path()));
    ASSERT_EQ(ZX_OK, path.Push("foo/ba/z/qu"));

    EXPECT_EQ(ZX_OK, path.Ensure(""));
    EXPECT_NE(ZX_OK, path.Ensure("x"));
    EXPECT_EQ(ZX_OK, path.Ensure("ux"));
    EXPECT_EQ(ZX_OK, path.Ensure("corge"));
    EXPECT_EQ(ZX_OK, path.Ensure("g/rault"));
    EXPECT_EQ(ZX_OK, path.Ensure("g/arply"));

    EXPECT_NE(ZX_OK, path.Remove(""));
    EXPECT_EQ(ZX_OK, path.Remove("a"));

    EXPECT_EQ(ZX_OK, path.Remove("x"));
    EXPECT_NE(ZX_OK, path.Push("x"));

    EXPECT_EQ(ZX_OK, path.Remove("corge"));
    EXPECT_NE(ZX_OK, path.Push("corge"));

    EXPECT_EQ(ZX_OK, path.Remove("g"));
    EXPECT_NE(ZX_OK, path.Push("g"));

    path.Pop();
    EXPECT_EQ(ZX_OK, path.Remove("foo"));
    EXPECT_NE(ZX_OK, path.Push("foo"));

    END_TEST;
}

bool TestRename() {
    BEGIN_TEST;
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path("foo/ba")));

    EXPECT_NE(ZX_OK, path.Rename("", "empty"));
    EXPECT_NE(ZX_OK, path.Rename("empty", ""));

    EXPECT_NE(ZX_OK, path.Rename("missing", "found"));

    fbl::unique_fd fd(open(fixture.path("foo/ba/r").c_str(), O_RDWR));
    EXPECT_TRUE(!!fd);
    fd.reset(open(fixture.path("foo/ba/s").c_str(), O_RDWR));
    EXPECT_FALSE(!!fd);

    EXPECT_EQ(ZX_OK, path.Rename("r", "s"));
    fd.reset(open(fixture.path("foo/ba/r").c_str(), O_RDWR));
    EXPECT_FALSE(!!fd);
    fd.reset(open(fixture.path("foo/ba/s").c_str(), O_RDWR));
    EXPECT_TRUE(!!fd);

    EXPECT_EQ(ZX_OK, path.Rename("s", "r"));
    fd.reset(open(fixture.path("foo/ba/r").c_str(), O_RDWR));
    EXPECT_TRUE(!!fd);
    fd.reset(open(fixture.path("foo/ba/s").c_str(), O_RDWR));
    EXPECT_FALSE(!!fd);

    EXPECT_EQ(ZX_OK, path.Rename("z", "y"));
    EXPECT_NE(ZX_OK, path.Push("z/qu/ux"));
    EXPECT_EQ(ZX_OK, path.Push("y/qu/ux"));

    path.Pop();
    EXPECT_EQ(ZX_OK, path.Rename("y", "z"));
    EXPECT_NE(ZX_OK, path.Push("y/qu/ux"));
    EXPECT_EQ(ZX_OK, path.Push("z/qu/ux"));

    END_TEST;
}

bool TestReset() {
    BEGIN_TEST;
    PathFixture fixture;
    ASSERT_TRUE(fixture.Create());

    Path path;
    ASSERT_EQ(ZX_OK, path.Push(fixture.path()));

    path.Reset();
    EXPECT_STR_EQ(path.c_str(), "/");

    END_TEST;
}

BEGIN_TEST_CASE(PathTest)
RUN_TEST(TestJoin)
RUN_TEST(TestPushAndPop)
RUN_TEST(TestGetSizeAndExists)
RUN_TEST(TestList)
RUN_TEST(TestEnsureAndRemove)
RUN_TEST(TestRename)
RUN_TEST(TestReset)
END_TEST_CASE(PathTest)

} // namespace
} // namespace testing
} // namespace fuzzing
