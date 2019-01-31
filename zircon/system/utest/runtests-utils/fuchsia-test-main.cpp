// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Test main for runtests-utils test on Fuchsia.

#include "runtests-utils-test-globals.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/memfs/memfs.h>
#include <runtests-utils/fuchsia-run-test.h>
#include <unittest/unittest.h>

namespace runtests {

// Root directory of memfs installed for duration of test.
static constexpr char kMemFsRoot[] = "/test-memfs";

const char kScriptShebang[32] = "#!/boot/bin/sh\n\n";
const RunTestFn PlatformRunTest = &FuchsiaRunTest;

const char* TestFsRoot() {
    return kMemFsRoot;
}

} // namespace runtests

int main(int argc, char** argv) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
    if (loop.StartThread() != ZX_OK) {
        fprintf(stderr, "Error: Cannot initialize local memfs loop\n");
        return -1;
    }
    if (memfs_install_at(loop.dispatcher(), runtests::kMemFsRoot) != ZX_OK) {
        fprintf(stderr, "Error: Cannot install local memfs\n");
        return -1;
    }
    return unittest_run_all_tests(argc, argv) ? EXIT_SUCCESS : EXIT_FAILURE;
}
