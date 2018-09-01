// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include <unittest/unittest.h>

#include "fuzzer-fixture.h"
#include "test-fuzzer.h"

namespace fuzzing {
namespace testing {
namespace {

// See fuzzer-fixture.cpp for the location and contents of test files.

bool TestSetOption() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    EXPECT_NE(ZX_OK, test.SetOption("", "value1"));
    EXPECT_NE(ZX_OK, test.SetOption("key1", ""));

    // Value isn't set
    const char* value = test.GetOption("key1");
    EXPECT_NULL(value);

    // Empty options are ignored
    EXPECT_EQ(ZX_OK, test.SetOption("", ""));
    EXPECT_EQ(ZX_OK, test.SetOption(""));
    EXPECT_EQ(ZX_OK, test.SetOption("# A comment"));
    EXPECT_EQ(ZX_OK, test.SetOption("   # A comment with leading whitespace"));

    // Set some values normally
    EXPECT_EQ(ZX_OK, test.SetOption("key1", "value1"));
    EXPECT_EQ(ZX_OK, test.SetOption("key2", "value2"));
    EXPECT_EQ(ZX_OK, test.SetOption("key3=value3"));
    EXPECT_EQ(ZX_OK, test.SetOption("\t -key4 \t=\t value4 \t# A comment"));

    // Check values
    value = test.GetOption("key1");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value1");

    value = test.GetOption("key2");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value2");

    value = test.GetOption("key3");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value3");

    value = test.GetOption("key4");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value4");

    // Replace each option
    EXPECT_EQ(ZX_OK, test.SetOption("key3", "value4"));
    EXPECT_EQ(ZX_OK, test.SetOption("key2=value3"));
    EXPECT_EQ(ZX_OK, test.SetOption(" \t-key1\t = \tvalue2\t # A comment"));
    EXPECT_EQ(ZX_OK, test.SetOption("key4", "value1"));

    // Check values
    value = test.GetOption("key1");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value2");

    value = test.GetOption("key2");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value3");

    value = test.GetOption("key3");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value4");

    value = test.GetOption("key4");
    ASSERT_NONNULL(value);
    EXPECT_STR_EQ(value, "value1");

    // Must be key value pair
    EXPECT_NE(ZX_OK, test.SetOption("key1", ""));
    EXPECT_NE(ZX_OK, test.SetOption("", "value2"));
    EXPECT_NE(ZX_OK, test.SetOption("key3"));
    EXPECT_NE(ZX_OK, test.SetOption("key5=#value5"));

    END_TEST;
}

bool TestRebasePath() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());
    const FuzzerFixture& fixture = test.fixture();

    Path path;
    EXPECT_EQ(ZX_OK, test.RebasePath("boot", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path("boot/").c_str());

    EXPECT_EQ(ZX_OK, test.RebasePath("boot/test/fuzz", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path("boot/test/fuzz/").c_str());

    EXPECT_NE(ZX_OK, test.RebasePath("pkgfs", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    END_TEST;
}

bool TestGetPackagePath() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitFuchsia());
    const FuzzerFixture& fixture = test.fixture();

    Path path;
    fbl::String expected;
    EXPECT_NE(ZX_OK, test.GetPackagePath("", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    EXPECT_NE(ZX_OK, test.GetPackagePath("not-a-package", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    const char* package = "zircon_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_STR_EQ(
        path.c_str(),
        fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)).c_str());

    EXPECT_NE(ZX_OK, test.GetPackagePath("fuchsia", &path));
    EXPECT_STR_EQ(path.c_str(), fixture.path().c_str());

    package = "fuchsia1_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_STR_EQ(
        path.c_str(),
        fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)).c_str());

    package = "fuchsia2_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_STR_EQ(
        path.c_str(),
        fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)).c_str());

    END_TEST;
}

bool TestFindZirconFuzzers() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    StringMap fuzzers;
    test.FindZirconFuzzers("no/such/dir", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindZirconFuzzers("boot/test/fuzz", "no-such", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    // Empty matches all
    test.FindZirconFuzzers("boot/test/fuzz", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    // Idempotent
    test.FindZirconFuzzers("boot/test/fuzz", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    // Substrings match
    fuzzers.clear();
    test.FindZirconFuzzers("boot/test/fuzz", "target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    fuzzers.clear();
    test.FindZirconFuzzers("boot/test/fuzz", "1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target2"));

    END_TEST;
}

bool TestFindFuchsiaFuzzers() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitFuchsia());

    StringMap fuzzers;
    test.FindFuchsiaFuzzers("not-a-package", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindFuchsiaFuzzers("", "not-a-target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    // Empty matches all
    test.FindFuchsiaFuzzers("", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Idempotent
    test.FindFuchsiaFuzzers("", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Substrings match
    fuzzers.clear();
    test.FindFuchsiaFuzzers("fuchsia", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuchsiaFuzzers("", "target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuchsiaFuzzers("fuchsia", "target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuchsiaFuzzers("", "1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));

    fuzzers.clear();
    test.FindFuchsiaFuzzers("1", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 3);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));

    fuzzers.clear();
    test.FindFuchsiaFuzzers("1", "4", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    fuzzers.clear();
    test.FindFuchsiaFuzzers("2", "1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    END_TEST;
}

bool TestFindFuzzers() {
    BEGIN_TEST;

    // Zircon tests
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    // Empty matches all
    StringMap fuzzers;
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    // Idempotent
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    // Substrings match
    fuzzers.clear();
    test.FindFuzzers("invalid", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindFuzzers("fuchsia", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    fuzzers.clear();
    test.FindFuzzers("zircon", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    fuzzers.clear();
    test.FindFuzzers("1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));

    // Fuchsia tests
    ASSERT_TRUE(test.InitFuchsia());

    // Empty matches all
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 6);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Idempotent
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 6);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Substrings match
    fuzzers.clear();
    test.FindFuzzers("fuzzers/no-such-target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindFuzzers("no-such-package/target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindFuzzers("zircon", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));

    fuzzers.clear();
    test.FindFuzzers("fuchsia", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("fuchsia2", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("fuchsia", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("_fuzzers/target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 6);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));

    fuzzers.clear();
    test.FindFuzzers("1/", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 3);
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));

    fuzzers.clear();
    test.FindFuzzers("/1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));

    END_TEST;
}

bool TestCheckProcess() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    EXPECT_FALSE(test.CheckProcess(ZX_HANDLE_INVALID));
    EXPECT_FALSE(test.CheckProcess(zx_process_self()));

    char name[ZX_MAX_NAME_LEN];
    ASSERT_EQ(ZX_OK, zx_object_get_property(zx_process_self(), ZX_PROP_NAME, name, sizeof(name)));

    EXPECT_TRUE(test.CheckProcess(zx_process_self(), name));

    END_TEST;
}

bool TestInvalid() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.InitZircon());

    ASSERT_TRUE(test.Eval(""));
    EXPECT_NE(ZX_OK, test.Run());
    ASSERT_TRUE(test.Eval("bad"));
    EXPECT_NE(ZX_OK, test.Run());

    END_TEST;
}

BEGIN_TEST_CASE(FuzzerTest)
RUN_TEST(TestSetOption)
RUN_TEST(TestRebasePath)
RUN_TEST(TestGetPackagePath)
RUN_TEST(TestFindZirconFuzzers)
RUN_TEST(TestFindFuchsiaFuzzers)
RUN_TEST(TestFindFuzzers)
RUN_TEST(TestCheckProcess)
RUN_TEST(TestInvalid)
END_TEST_CASE(FuzzerTest)

} // namespace
} // namespace testing
} // namespace fuzzing
