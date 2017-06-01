// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unittest/unittest.h>

__BEGIN_CDECLS

/**
 * Starts a thread for the crash handler and runs the test.
 * Returns true if the test succeeds and no unexpected crashes occurred.
 */
bool run_test_with_crash_handler(struct test_info* current_test_info,
                                 bool (*test_to_run)(void));

__END_CDECLS

