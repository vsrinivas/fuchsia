// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/memory_probe.h>

#include <unittest/unittest.h>

namespace {

bool probe_readwrite() {
    BEGIN_TEST;

    int valid = 0;
    EXPECT_TRUE(probe_for_read(&valid));
    EXPECT_TRUE(probe_for_write(&valid));

    END_TEST;
}

bool probe_readonly() {
    BEGIN_TEST;

    // This uses the address of this function. This assumes that the code section is readable but
    // not writable.
    void* this_function = reinterpret_cast<void*>(&probe_readonly);
    EXPECT_TRUE(probe_for_read(this_function));
    EXPECT_FALSE(probe_for_write(this_function));

    END_TEST;
}

bool probe_invalid() {
    BEGIN_TEST;

    EXPECT_FALSE(probe_for_read(nullptr));
    EXPECT_FALSE(probe_for_write(nullptr));

    END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(memory_probe_tests)
RUN_TEST(probe_readwrite)
RUN_TEST(probe_readonly)
RUN_TEST(probe_invalid)
END_TEST_CASE(memory_probe_tests)

