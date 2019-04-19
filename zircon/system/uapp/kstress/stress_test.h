// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdarg.h>

#include <fbl/macros.h>
#include <fbl/vector.h>
#include <fbl/unique_ptr.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

class StressTest {
public:
    StressTest() {
        tests_.push_back(this);
    }

    virtual ~StressTest() = default;

    DISALLOW_COPY_ASSIGN_AND_MOVE(StressTest);

    // Called once before starting the test. Allocate resources needed for
    // the test here.
    //
    // If overridden in a subclass, call through to this version first.
    virtual zx_status_t Init(bool verbose, const zx_info_kmem_stats& stats) {
        verbose_ = verbose;

        // gather some info about the system
        kmem_stats_ = stats;
        num_cpus_ = zx_system_get_num_cpus();
        return ZX_OK;
    }

    // Called once to start the test. Must return immediately.
    virtual zx_status_t Start() = 0;

    // Called to stop the individual test. Must wait until test has
    // been shut down.
    virtual zx_status_t Stop() = 0;

    // Return the name of the test in C string format
    virtual const char* name() const = 0;

    // get a ref to the master test list
    static fbl::Vector<StressTest*>& tests() { return tests_; }

    // wrapper around printf that enables/disables based on verbose flag
    void Printf(const char *fmt, ...) const {
        if (!verbose_) {
            return;
        }

        va_list ap;
        va_start(ap, fmt);

        vprintf(fmt, ap);

        va_end(ap);
    }

    void PrintfAlways(const char *fmt, ...) const {
        va_list ap;
        va_start(ap, fmt);

        vprintf(fmt, ap);

        va_end(ap);
    }

protected:
    // global list of all the stress tests, registered at app start
    static fbl::Vector<StressTest*> tests_;

    bool verbose_{false};
    zx_info_kmem_stats_t kmem_stats_{};
    uint32_t num_cpus_{};
};

// factories for local tests
fbl::unique_ptr<StressTest> CreateVmStressTest();
