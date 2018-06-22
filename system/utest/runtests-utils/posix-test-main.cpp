// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test main for the runtests-utils test on POSIX systems (e.g., Linux and
// MacOS).

#include "runtests-utils-test-globals.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <runtests-utils/posix-run-test.h>
#include <unittest/unittest.h>

namespace runtests {

// Pointer to root of unique subdirectory of TMPDIR or /tmp.
static const fbl::String* TmpDirRoot = nullptr;

const char kScriptShebang[32] = "#!/bin/sh\n\n";
const RunTestFn PlatformRunTest = &PosixRunTest;

const char* TestFsRoot() {
    if (TmpDirRoot == nullptr) {
        char test_fs_template[256];
        sprintf(test_fs_template, "%s/XXXXXX",
                getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
        TmpDirRoot = new fbl::String(mkdtemp(test_fs_template));
    }
    return TmpDirRoot->c_str();
}

} // namespace runtests

int main(int argc, char** argv) {
    printf("\nRoot directory of the filesystem used for testing: %s\n",
           runtests::TestFsRoot());

    auto auto_test_fs_clean_up = fbl::MakeAutoCall([&] {
        // Since all subdirectories created will be scoped, at the end of the
        // test TestFsRoot() should be empty.
        remove(runtests::TestFsRoot());
        delete runtests::TmpDirRoot;
    });
    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
