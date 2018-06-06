// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_single_list.h>
#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/status.h>
#include <zircon/syscalls/object.h>

class StressTest : public fbl::SinglyLinkedListable<fbl::unique_ptr<StressTest>> {
public:
    StressTest() = default;
    virtual ~StressTest() = default;

    DISALLOW_COPY_ASSIGN_AND_MOVE(StressTest);

    // Called once before starting the test. Allocate resources needed for
    // the test here.
    //
    // If overridden in a subclass, call through to this version first.
    virtual zx_status_t Init(const zx_info_kmem_stats& stats) {
        // gather some info about the system
        kmem_stats_ = stats;
        num_cpus_ = zx_system_get_num_cpus();
        return ZX_OK;
    }

    // Called once to start the test. Must return immediately.
    virtual zx_status_t Start() = 0;

    // Called to stop the inividual test. Must wait until test has
    // been shut down.
    virtual zx_status_t Stop() = 0;

protected:
    zx_info_kmem_stats_t kmem_stats_{};
    uint32_t num_cpus_{};
};

// factories for local tests
fbl::unique_ptr<StressTest> CreateVmStressTest();
