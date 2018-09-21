// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <fbl/string_printf.h>
#include <unittest/unittest.h>

#include "fuzzer-fixture.h"

namespace fuzzing {
namespace testing {

// Public methods

bool FuzzerFixture::CreateZircon() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    // Zircon binaries
    ASSERT_TRUE(CreateFile("boot/test/fuzz/target1"));
    ASSERT_TRUE(CreateFile("boot/test/fuzz/target2"));

    // Data from "previous" runs
    ASSERT_TRUE(CreateData("zircon_fuzzers", "target2"));

    END_HELPER;
}

bool FuzzerFixture::CreateFuchsia() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    // Zircon binaries
    ASSERT_TRUE(CreateFile("system/test/fuzz/target1"));
    ASSERT_TRUE(CreateFile("system/test/fuzz/target2"));

    // Fuchsia packages
    ASSERT_TRUE(CreatePackage("zircon_fuzzers", 0, "target2"));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 1, "target1"));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 2, "target1"));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target1"));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target2"));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target3"));
    ASSERT_TRUE(CreatePackage("fuchsia2_fuzzers", 2, "target4"));
    ASSERT_TRUE(CreatePackage("fuchsia2_fuzzers", 5, "target4"));
    ASSERT_TRUE(CreatePackage("fuchsia2_fuzzers", 10, "target4"));

    // Data from "previous" runs
    ASSERT_TRUE(CreateData("zircon_fuzzers", "target2"));
    ASSERT_TRUE(CreateData("fuchsia2_fuzzers", "target4"));

    END_HELPER;
}

// Protected methods

void FuzzerFixture::Reset() {
    max_versions_.clear();
    Fixture::Reset();
}

// Private methods

bool FuzzerFixture::CreatePackage(const char* package, long int version, const char* target) {
    BEGIN_HELPER;

    const char* max = max_version(package);
    if (!max || strtol(max, nullptr, 0) < version) {
        max_versions_.set(package, fbl::StringPrintf("%ld", version));
    }

    if (strcmp(package, "zircon_fuzzers") != 0) {
        ASSERT_TRUE(CreateFile(path("pkgfs/packages/%s/%ld/bin/%s", package, version, target)));
    }

    ASSERT_TRUE(CreateFile(path("pkgfs/packages/%s/%ld/meta/%s.cmx", package, version, target)));

    ASSERT_TRUE(CreateFile(path("pkgfs/packages/%s/%ld/data/%s/corpora", package, version, target),
                           "//path/to/seed/corpus\n "
                           "//path/to/cipd/ensure/file\n"
                           "https://gcs/url\n"));
    ASSERT_TRUE(CreateFile(
        path("pkgfs/packages/%s/%ld/data/%s/dictionary", package, version, target), "foo\n"
                                                                                    "bar\n"
                                                                                    "baz\n"));
    ASSERT_TRUE(CreateFile(path("pkgfs/packages/%s/%ld/data/%s/options", package, version, target),
                           "foo = bar\n"
                           "baz = qux\n"));

    END_HELPER;
}

bool FuzzerFixture::CreateData(const char* package, const char* target) {
    BEGIN_HELPER;

    ASSERT_TRUE(CreateDirectory(path("data/fuzzing/%s/%s/corpus", package, target)));
    ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/crash-deadbeef", package, target)));
    ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/leak-deadfa11", package, target)));
    ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/oom-feedface", package, target)));

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
