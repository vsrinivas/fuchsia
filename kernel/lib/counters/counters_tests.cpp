// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>

#include "counters_private.h"

static bool value_cleanup() {
    BEGIN_TEST;

    uint64_t outputs[SMP_MAX_CPUS];
    size_t out_count;

    // Sorted.
    uint64_t inputs0[SMP_MAX_CPUS] = {13, 4, 8, 9};
    counters_clean_up_values(inputs0, outputs, &out_count);
    ASSERT_EQ(out_count, 4u, "");
    EXPECT_EQ(outputs[0], 4u, "");
    EXPECT_EQ(outputs[1], 8u, "");
    EXPECT_EQ(outputs[2], 9u, "");
    EXPECT_EQ(outputs[3], 13u, "");

    // Collapsed to remove zeros.
    uint64_t inputs1[SMP_MAX_CPUS] = {13, 0, 0, 9};
    counters_clean_up_values(inputs1, outputs, &out_count);
    ASSERT_EQ(out_count, 2u, "");
    EXPECT_EQ(outputs[0], 9u, "");
    EXPECT_EQ(outputs[1], 13u, "");

    END_TEST;
}

// Data to compare vs. results in
// https://docs.google.com/spreadsheets/d/1D58chwOpO-3_c41NMGJkpmFuOpGSYH-W50bD6MdOAjo/edit?usp=sharing
static uint64_t test_counters_inputs0[SMP_MAX_CPUS] = {5105, 4602, 4031, 4866};
static uint64_t test_counters_inputs1[SMP_MAX_CPUS] = {3524, 3461, 3567, 2866};

static bool percentile_determination() {
    BEGIN_TEST;

    uint64_t cleaned[SMP_MAX_CPUS];
    size_t out_count;

    counters_clean_up_values(test_counters_inputs0, cleaned, &out_count);
    EXPECT_EQ(counters_get_percentile(cleaned, out_count, /*0.25*/ 64),
              /* 4459.25 */ (4459u << 8) + 64u, "");
    EXPECT_EQ(counters_get_percentile(cleaned, out_count, /*0.75*/ 192),
              /* 4925.75 */ (4925u << 8) + 192u, "");

    counters_clean_up_values(test_counters_inputs1, cleaned, &out_count);
    EXPECT_EQ(counters_get_percentile(cleaned, out_count, /*0.25*/ 64),
              /* 3312.25 */ (3312u << 8) + 64u, "");
    EXPECT_EQ(counters_get_percentile(cleaned, out_count, /*0.75*/ 192),
              /* 3534.75 */ (3534u << 8) + 192u, "");

    END_TEST;
}

static bool outlier_check() {
    BEGIN_TEST;

    uint64_t no_values[SMP_MAX_CPUS] = {0};
    EXPECT_FALSE(counters_has_outlier(no_values), "0 values shouldn't have outlier");

    uint64_t one_value[SMP_MAX_CPUS] = {789};
    EXPECT_FALSE(counters_has_outlier(one_value), "1 value shouldn't have outlier");

    EXPECT_FALSE(counters_has_outlier(test_counters_inputs0), "");
    EXPECT_TRUE(counters_has_outlier(test_counters_inputs1), "");

    END_TEST;
}

UNITTEST_START_TESTCASE(counters_tests)
UNITTEST("value cleanup", value_cleanup)
UNITTEST("percentile determination", percentile_determination)
UNITTEST("outlier check", outlier_check)
UNITTEST_END_TESTCASE(counters_tests, "counters_tests", "Counters tests");
