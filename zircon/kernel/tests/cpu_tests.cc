// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

// Contains tests for kernel/include/kernel/cpu.h

#include <bits.h>
#include <lib/unittest/unittest.h>

#include <kernel/cpu.h>
#include <ktl/bit.h>

#include <ktl/enforce.h>

namespace {

bool remove_cpu_from_mask_test() {
  BEGIN_TEST;

  {
    // Empty.
    cpu_mask_t mask = 0;
    EXPECT_EQ(INVALID_CPU, remove_cpu_from_mask(mask));
  }

  {
    // Full.
    const cpu_mask_t full_mask = BIT_MASK(SMP_MAX_CPUS);
    cpu_mask_t mask = full_mask;
    cpu_mask_t result = 0;
    while (mask != 0u) {
      const cpu_mask_t prev_mask = mask;
      cpu_num_t cpu = remove_cpu_from_mask(mask);
      // Make sure it's valid.
      EXPECT_NE(INVALID_CPU, cpu);
      EXPECT_TRUE(is_valid_cpu_num(cpu));
      // Make sure it was removed.  If not, abort the test so we don't loop forever.
      ASSERT_FALSE(mask & cpu_num_to_mask(cpu));
      // Make sure nothing else was removed.
      EXPECT_EQ(prev_mask, mask | cpu_num_to_mask(cpu));
      // Add it to our result set.
      result |= cpu_num_to_mask(cpu);
    }
    // See that the result set is complete.
    EXPECT_EQ(full_mask, result);
  }

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(cpu_tests)
UNITTEST("remove_cpu_from_mask", remove_cpu_from_mask_test)
UNITTEST_END_TESTCASE(cpu_tests, "cpu", "cpu tests")
