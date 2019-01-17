// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <unittest/unittest.h>

#include "coordinator.h"

namespace devmgr {
    zx::channel fs_clone(const char* path) {
        return zx::channel();
    }
}

bool construct() {
    BEGIN_TEST;

    devmgr::CoordinatorConfig config;
    config.dispatcher = nullptr;
    config.require_system = false;
    config.asan_drivers = false;
    devmgr::Coordinator coordinator(std::move(config));

    END_TEST;
}

BEGIN_TEST_CASE(coordinator_tests)
RUN_TEST_SMALL(construct)
END_TEST_CASE(coordinator_tests)
