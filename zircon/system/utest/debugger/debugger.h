// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// The RQST_START_*_THREADS requests start this many threads.
constexpr int kNumExtraThreads = 4;

// The CRASH_AND_RECOVER_TEST request causes this many segvs.
// We do the segv recovery test a number of times to stress test the API.
constexpr int kNumSegvTries = 4;

bool stopped_in_thread_starting_reg_access_test();

int capture_regs_thread_func(void* arg);
