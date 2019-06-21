// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <unittest/unittest.h>

#include "utils.h"

int main(int argc, char** argv) {
    ExternalThread::SetProgramName(argv[0]);

    if ((argc == 2) && !strcmp(argv[1], ExternalThread::helper_flag())) {
        return ExternalThread::DoHelperThread();
    }

    bool success = unittest_run_all_tests(argc, argv);
    return success ? 0 : -1;
}
