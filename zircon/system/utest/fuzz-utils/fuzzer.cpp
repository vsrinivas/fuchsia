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
    ASSERT_TRUE(test.Init());

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
    ASSERT_TRUE(test.Init());
    const FuzzerFixture& fixture = test.fixture();

    Path path;
    EXPECT_EQ(ZX_OK, test.RebasePath("boot", &path));
    EXPECT_CSTR_EQ(path, fixture.path("boot/"));

    EXPECT_EQ(ZX_OK, test.RebasePath("boot/test/fuzz", &path));
    EXPECT_CSTR_EQ(path, fixture.path("boot/test/fuzz/"));

    EXPECT_NE(ZX_OK, test.RebasePath("no-such-path", &path));
    EXPECT_CSTR_EQ(path, fixture.path());

    END_TEST;
}

bool TestGetPackagePath() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());
    const FuzzerFixture& fixture = test.fixture();

    Path path;
    fbl::String expected;
    EXPECT_NE(ZX_OK, test.GetPackagePath("", &path));
    EXPECT_CSTR_EQ(path, fixture.path());

    EXPECT_NE(ZX_OK, test.GetPackagePath("not-a-package", &path));
    EXPECT_CSTR_EQ(path, fixture.path());

    const char* package = "zircon_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_CSTR_EQ(path,
                   fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)));

    EXPECT_NE(ZX_OK, test.GetPackagePath("fuchsia", &path));
    EXPECT_CSTR_EQ(path, fixture.path());

    package = "fuchsia1_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_CSTR_EQ(path,
                   fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)));

    package = "fuchsia2_fuzzers";
    EXPECT_EQ(ZX_OK, test.GetPackagePath(package, &path));
    EXPECT_CSTR_EQ(path,
                   fixture.path("pkgfs/packages/%s/%s/", package, fixture.max_version(package)));

    END_TEST;
}

bool TestFindFuzzers() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    // FindFuzzers with package/target
    StringMap fuzzers;
    test.FindFuzzers("not-a-package", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    test.FindFuzzers("", "not-a-target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    // In the tests below, "zircon_fuzzers/target1" does not correspond to a package (just a
    // binary).  All others do correspond to packages. See fuzzer-fixture.cpp for more details.

    // Empty matches all
    test.FindFuzzers("", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Idempotent
    test.FindFuzzers("", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Substrings match
    fuzzers.clear();
    test.FindFuzzers("fuchsia", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("", "target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("fuchsia", "target", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 4);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("", "2", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 2);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("1", "", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 3);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("1", "4", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    fuzzers.clear();
    test.FindFuzzers("2", "1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 0);

    // FindFuzzers using 'name'
    // Empty matches all
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    // Idempotent
    test.FindFuzzers("", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
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
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
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
    EXPECT_EQ(fuzzers.size(), 5);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("zircon_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target2"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target3"));
    EXPECT_NONNULL(fuzzers.get("fuchsia2_fuzzers/target4"));

    fuzzers.clear();
    test.FindFuzzers("1", &fuzzers);
    EXPECT_EQ(fuzzers.size(), 3);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
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
    EXPECT_EQ(fuzzers.size(), 1);
    EXPECT_NULL(fuzzers.get("zircon_fuzzers/target1"));
    EXPECT_NONNULL(fuzzers.get("fuchsia1_fuzzers/target1"));

    END_TEST;
}

bool TestCheckProcess() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

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
    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.Eval(""));
    EXPECT_NE(ZX_OK, test.Run());
    ASSERT_TRUE(test.Eval("bad"));
    EXPECT_NE(ZX_OK, test.Run());

    END_TEST;
}

bool TestHelp() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.Eval("help"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("help"));
    EXPECT_TRUE(test.InStdOut("list"));
    EXPECT_TRUE(test.InStdOut("seeds"));
    EXPECT_TRUE(test.InStdOut("start"));
    EXPECT_TRUE(test.InStdOut("check"));
    EXPECT_TRUE(test.InStdOut("stop"));
    EXPECT_TRUE(test.InStdOut("repro"));
    EXPECT_TRUE(test.InStdOut("merge"));

    END_TEST;
}

bool TestList() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    // In the tests below, "zircon_fuzzers/target1" does not correspond to a package (just a
    // binary).  All others do correspond to packages. See fuzzer-fixture.cpp for more details.

    ASSERT_TRUE(test.Eval("list"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target1"));
    EXPECT_TRUE(test.InStdOut("zircon_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target1"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target3"));
    EXPECT_TRUE(test.InStdOut("fuchsia2_fuzzers/target4"));

    ASSERT_TRUE(test.Eval("list fuchsia"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target1"));
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target1"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target3"));
    EXPECT_TRUE(test.InStdOut("fuchsia2_fuzzers/target4"));

    ASSERT_TRUE(test.Eval("list target"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target1"));
    EXPECT_TRUE(test.InStdOut("zircon_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target1"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target3"));
    EXPECT_TRUE(test.InStdOut("fuchsia2_fuzzers/target4"));

    ASSERT_TRUE(test.Eval("list fuchsia1_fuzzers/target1"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target1"));
    EXPECT_FALSE(test.InStdOut("zircon_fuzzers/target2"));
    EXPECT_TRUE(test.InStdOut("fuchsia1_fuzzers/target1"));
    EXPECT_FALSE(test.InStdOut("fuchsia1_fuzzers/target2"));
    EXPECT_FALSE(test.InStdOut("fuchsia1_fuzzers/target3"));
    EXPECT_FALSE(test.InStdOut("fuchsia2_fuzzers/target4"));

    END_TEST;
}

bool TestSeeds() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.Eval("seeds zircon/target2"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("//path/to/seed/corpus"));
    EXPECT_TRUE(test.InStdOut("//path/to/cipd/ensure/file"));
    EXPECT_TRUE(test.InStdOut("https://gcs/url"));

    ASSERT_TRUE(test.Eval("seeds fuchsia1/target3"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("//path/to/seed/corpus"));
    EXPECT_TRUE(test.InStdOut("//path/to/cipd/ensure/file"));
    EXPECT_TRUE(test.InStdOut("https://gcs/url"));

    END_TEST;
}

bool TestStart() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    // Zircon fuzzer within Fuchsia
    ASSERT_TRUE(test.Eval("start zircon/target2"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-jobs=1"));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-baz=qux"));
    EXPECT_LT(0, test.FindArg("-dict=%s", test.dictionary()));
    EXPECT_LT(0, test.FindArg("-foo=bar"));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));

    // Fuchsia fuzzer without resources
    ASSERT_TRUE(test.Eval("start fuchsia1/target1"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));

    // Fuchsia fuzzer with resources
    ASSERT_TRUE(test.Eval("start fuchsia1/target3"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-jobs=1"));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-baz=qux"));
    EXPECT_LT(0, test.FindArg("-dict=%s", test.dictionary()));
    EXPECT_LT(0, test.FindArg("-foo=bar"));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));

    // Fuchsia fuzzer with resources, command-line option, and explicit corpus
    ASSERT_TRUE(test.Eval("start fuchsia2/target4 /path/to/another/corpus -foo=baz"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-jobs=1"));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-baz=qux"));
    EXPECT_LT(0, test.FindArg("-dict=%s", test.dictionary()));
    EXPECT_LT(0, test.FindArg("-foo=baz"));
    EXPECT_LT(0, test.FindArg("/path/to/another/corpus"));
    EXPECT_GT(0, test.FindArg(test.data_path("corpus")));

    END_TEST;
}

bool TestCheck() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.Eval("check zircon/target2"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("stopped"));
    EXPECT_TRUE(test.InStdOut(test.executable()));
    EXPECT_TRUE(test.InStdOut(test.data_path()));
    EXPECT_TRUE(test.InStdOut("0 inputs"));
    EXPECT_TRUE(test.InStdOut("crash"));

    ASSERT_TRUE(test.Eval("check fuchsia/target1"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("stopped"));
    EXPECT_TRUE(test.InStdOut(test.executable()));
    EXPECT_TRUE(test.InStdOut(test.data_path()));
    EXPECT_TRUE(test.InStdOut("0 inputs"));
    EXPECT_TRUE(test.InStdOut("none"));

    ASSERT_TRUE(test.Eval("check fuchsia/target4"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("stopped"));
    EXPECT_TRUE(test.InStdOut(test.executable()));
    EXPECT_TRUE(test.InStdOut(test.data_path()));
    EXPECT_TRUE(test.InStdOut("0 inputs"));
    EXPECT_TRUE(test.InStdOut("crash"));

    END_TEST;
}

bool TestStop() {
    BEGIN_TEST;
    TestFuzzer test;

    ASSERT_TRUE(test.Init());

    ASSERT_TRUE(test.Eval("stop"));
    EXPECT_NE(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdErr("missing"));

    ASSERT_TRUE(test.Eval("stop foobar"));
    EXPECT_NE(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdErr("no match"));

    ASSERT_TRUE(test.Eval("stop target"));
    EXPECT_NE(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdErr("multiple"));

    ASSERT_TRUE(test.Eval("stop zircon/target2"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdOut("stopped"));

    END_TEST;
}

bool TestRepro() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    // Zircon fuzzer within Fuchsia
    ASSERT_TRUE(test.Eval("repro zircon/target2 fa"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-baz=qux"));
    EXPECT_LT(0, test.FindArg("-dict=%s", test.dictionary()));
    EXPECT_LT(0, test.FindArg("-foo=bar"));
    EXPECT_LT(0, test.FindArg(test.data_path("leak-deadfa11")));
    EXPECT_LT(0, test.FindArg(test.data_path("oom-feedface")));
    EXPECT_GT(0, test.FindArg(test.data_path("crash-deadbeef")));
    EXPECT_GT(0, test.FindArg(test.data_path("corpus")));

    ASSERT_TRUE(test.Eval("repro fuchsia1/target1"));
    EXPECT_NE(ZX_OK, test.Run());
    EXPECT_TRUE(test.InStdErr("no match"));

    // Fuchsia fuzzer with resources
    ASSERT_TRUE(test.Eval("repro fuchsia2/target4"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-baz=qux"));
    EXPECT_LT(0, test.FindArg("-dict=%s", test.dictionary()));
    EXPECT_LT(0, test.FindArg("-foo=bar"));
    EXPECT_LT(0, test.FindArg(test.data_path("leak-deadfa11")));
    EXPECT_LT(0, test.FindArg(test.data_path("oom-feedface")));
    EXPECT_LT(0, test.FindArg(test.data_path("crash-deadbeef")));
    EXPECT_GT(0, test.FindArg(test.data_path("corpus")));

    END_TEST;
}

bool TestMerge() {
    BEGIN_TEST;
    TestFuzzer test;
    ASSERT_TRUE(test.Init());

    Path path;
    size_t len;

    // Zircon minimizing merge in Fuchsia
    ASSERT_TRUE(test.Eval("merge zircon/target2"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-merge=1"));
    EXPECT_LT(0, test.FindArg("-merge_control_file=%s", test.data_path(".mergefile")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus.prev")));

    path.Reset();
    ASSERT_EQ(ZX_OK, path.Push(test.data_path()));
    EXPECT_NE(ZX_OK, path.Push("corpus.prev"));
    EXPECT_NE(ZX_OK, path.GetSize(".mergefile", &len));

    // Fuchsia minimizing merge
    ASSERT_TRUE(test.Eval("merge fuchsia2/target4"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-merge=1"));
    EXPECT_LT(0, test.FindArg("-merge_control_file=%s", test.data_path(".mergefile")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus.prev")));

    path.Reset();
    ASSERT_EQ(ZX_OK, path.Push(test.data_path()));
    EXPECT_NE(ZX_OK, path.Push("corpus.prev"));
    EXPECT_NE(ZX_OK, path.GetSize(".mergefile", &len));

    // Fuchsia merge of another corpus without an existing corpus
    ASSERT_TRUE(test.Eval("merge fuchsia1/target3 /path/to/another/corpus"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-merge=1"));
    EXPECT_LT(0, test.FindArg("-merge_control_file=%s", test.data_path(".mergefile")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));
    EXPECT_LT(0, test.FindArg("/path/to/another/corpus"));

    path.Reset();
    ASSERT_EQ(ZX_OK, path.Push(test.data_path()));
    EXPECT_NE(ZX_OK, path.Push("corpus.prev"));
    EXPECT_NE(ZX_OK, path.GetSize(".mergefile", &len));

    // Fuchsia merge of another corpus with an existing corpus
    ASSERT_TRUE(test.Eval("merge fuchsia2/target4 /path/to/another/corpus"));
    EXPECT_EQ(ZX_OK, test.Run());
    EXPECT_EQ(0, test.FindArg(test.executable()));
    EXPECT_LT(0, test.FindArg(test.manifest()));
    EXPECT_LT(0, test.FindArg("-artifact_prefix=%s", test.data_path()));
    EXPECT_LT(0, test.FindArg("-merge=1"));
    EXPECT_LT(0, test.FindArg("-merge_control_file=%s", test.data_path(".mergefile")));
    EXPECT_LT(0, test.FindArg(test.data_path("corpus")));
    EXPECT_LT(0, test.FindArg("/path/to/another/corpus"));

    path.Reset();
    ASSERT_EQ(ZX_OK, path.Push(test.data_path()));
    EXPECT_NE(ZX_OK, path.Push("corpus.prev"));
    EXPECT_NE(ZX_OK, path.GetSize(".mergefile", &len));

    END_TEST;
}

BEGIN_TEST_CASE(FuzzerTest)
RUN_TEST(TestSetOption)
RUN_TEST(TestRebasePath)
RUN_TEST(TestGetPackagePath)
RUN_TEST(TestFindFuzzers)
RUN_TEST(TestCheckProcess)
RUN_TEST(TestInvalid)
RUN_TEST(TestHelp)
RUN_TEST(TestList)
RUN_TEST(TestSeeds)
RUN_TEST(TestStart)
RUN_TEST(TestCheck)
RUN_TEST(TestStop)
RUN_TEST(TestRepro)
RUN_TEST(TestMerge)
END_TEST_CASE(FuzzerTest)

} // namespace
} // namespace testing
} // namespace fuzzing
