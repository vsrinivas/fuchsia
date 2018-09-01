// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

#include <fbl/string_buffer.h>
#include <unittest/unittest.h>

#include "fuzzer-fixture.h"

namespace fuzzing {
namespace testing {

// Public methods

bool FuzzerFixture::CreateZircon() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    fbl::StringBuffer<PATH_MAX> buffer;
    buffer.AppendPrintf("boot/test/fuzz/target1");
    ASSERT_TRUE(CreateFile(buffer.c_str()));

    buffer.Clear();
    buffer.AppendPrintf("boot/test/fuzz/target2");
    ASSERT_TRUE(CreateFile(buffer.c_str()));
    ASSERT_TRUE(CreatePackage("zircon_fuzzers", 0, "target2", kHasData));

    END_HELPER;
}

bool FuzzerFixture::CreateFuchsia() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    fbl::StringBuffer<PATH_MAX> buffer;
    buffer.AppendPrintf("system/test/fuzz/target1");
    ASSERT_TRUE(CreateFile(buffer.c_str()));

    buffer.Clear();
    buffer.AppendPrintf("system/test/fuzz/target2");
    ASSERT_TRUE(CreateFile(buffer.c_str()));
    ASSERT_TRUE(CreatePackage("zircon_fuzzers", 0, "target2", kHasResources | kHasData));

    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 1, "target1", kHasBinary));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 2, "target1", kHasBinary));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target1", kHasBinary));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target2", kHasBinary));
    ASSERT_TRUE(CreatePackage("fuchsia1_fuzzers", 5, "target3", kHasBinary | kHasResources));
    ASSERT_TRUE(CreatePackage("fuchsia2_fuzzers", 2, "target4", kHasBinary | kHasResources));
    ASSERT_TRUE(CreatePackage("fuchsia2_fuzzers", 5, "target4", kHasBinary | kHasResources));
    ASSERT_TRUE(
        CreatePackage("fuchsia2_fuzzers", 10, "target4", kHasBinary | kHasResources | kHasData));

    END_HELPER;
}

// Protected methods

void FuzzerFixture::Reset() {
    max_versions_.clear();
    Fixture::Reset();
}

// Private methods

bool FuzzerFixture::CreatePackage(const char* package, long int version, const char* target,
                                  uint8_t flags) {
    BEGIN_HELPER;

    const char* max = max_version(package);
    if (!max || strtol(max, nullptr, 0) < version) {
        fbl::StringBuffer<20> buffer; // LONG_MAX has 19 digits
        buffer.AppendPrintf("%ld", version);
        max_versions_.set(package, buffer.c_str());
    }

    if (flags & kHasBinary) {
        ASSERT_TRUE(
            CreateFile(path("pkgfs/packages/%s/%ld/test/%s", package, version, target).c_str()));
    }
    if (flags & kHasResources) {
        ASSERT_TRUE(CreateFile(
            path("pkgfs/packages/%s/%ld/data/%s/corpora", package, version, target).c_str(),
            "//path/to/seed/corpus\n "
            "//path/to/cipd/ensure/file\n"
            "https://gcs/url\n"));
        ASSERT_TRUE(CreateFile(
            path("pkgfs/packages/%s/%ld/data/%s/dictionary", package, version, target).c_str(),
            "foo\n"
            "bar\n"
            "baz\n"));
        ASSERT_TRUE(CreateFile(
            path("pkgfs/packages/%s/%ld/data/%s/options", package, version, target).c_str(),
            "foo = bar\n"
            "baz = qux\n"));
    }
    if (flags & kHasData) {
        ASSERT_TRUE(CreateDirectory(path("data/fuzzing/%s/%s/corpus", package, target).c_str()));
        ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/crash-deadbeef", package, target).c_str()));
        ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/leak-deadfa11", package, target).c_str()));
        ASSERT_TRUE(CreateFile(path("data/fuzzing/%s/%s/oom-feedface", package, target).c_str()));
    }

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
