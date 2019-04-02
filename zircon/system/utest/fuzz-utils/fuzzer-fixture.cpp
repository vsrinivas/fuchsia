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

bool FuzzerFixture::Create() {
    BEGIN_HELPER;
    ASSERT_TRUE(Fixture::Create());

    // Zircon binaries without packages
    ASSERT_TRUE(CreateFile("boot/test/fuzz/target1"));

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

const char* FuzzerFixture::max_version(const char* package) const {
    const char* max = max_versions_.get(package);
    return max ? max : "0";
}

// Protected methods

void FuzzerFixture::Reset() {
    max_versions_.clear();
    Fixture::Reset();
}

// Private methods

bool FuzzerFixture::CreatePackage(const char* package, long int version, const char* target) {
    BEGIN_HELPER;
    auto base = fbl::StringPrintf("pkgfs/packages/%s/%ld", package, version);
    const char* max = max_version(package);
    if (!max || strtol(max, nullptr, 0) < version) {
        max_versions_.set(package, fbl::StringPrintf("%ld", version));
    }

    if (strcmp(package, "zircon_fuzzers") == 0) {
        ASSERT_TRUE(CreateFile(path("boot/test/fuzz/%s", target)));
    } else {
        ASSERT_TRUE(CreateFile(path("%s/bin/%s", base.c_str(), target)));
    }
    ASSERT_TRUE(CreateFile(path("%s/meta/%s.cmx", base.c_str(), target)));

    ASSERT_TRUE(CreateFile(path("%s/data/%s/corpora", base.c_str(), target),
                           "//path/to/seed/corpus\n "
                           "//path/to/cipd/ensure/file\n"
                           "https://gcs/url\n"));
    ASSERT_TRUE(CreateFile(path("%s/data/%s/dictionary", base.c_str(), target), "foo\n"
                                                                                "bar\n"
                                                                                "baz\n"));
    ASSERT_TRUE(CreateFile(path("%s/data/%s/options", base.c_str(), target), "foo = bar\n"
                                                                             "baz = qux\n"));

    END_HELPER;
}

bool FuzzerFixture::CreateData(const char* package, const char* target) {
    BEGIN_HELPER;

    fbl::String data_path =
        fbl::StringPrintf("data/r/sys/fuchsia.com:%s:0#meta:%s.cmx", package, target);
    ASSERT_TRUE(CreateDirectory(path("%s/corpus", data_path.c_str())));
    ASSERT_TRUE(CreateFile(path("%s/crash-deadbeef", data_path.c_str())));
    ASSERT_TRUE(CreateFile(path("%s/leak-deadfa11", data_path.c_str())));
    ASSERT_TRUE(CreateFile(path("%s/oom-feedface", data_path.c_str())));

    END_HELPER;
}

} // namespace testing
} // namespace fuzzing
