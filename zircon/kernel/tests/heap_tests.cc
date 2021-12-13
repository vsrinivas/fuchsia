// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <bits.h>
#include <lib/boot-options/boot-options.h>
#include <lib/unittest/unittest.h>
#include <zircon/errors.h>
#include <zircon/types.h>

static bool test_alloc_fill_threshold() {
  BEGIN_TEST;

  if (gBootOptions->alloc_fill_threshold) {
    size_t size = gBootOptions->alloc_fill_threshold - 1;
    char* const buffer = static_cast<char*>(malloc(size));
    for (size_t i = 0; i < size; i++) {
      EXPECT_EQ(0, buffer[i]);
    }
    free(buffer);
  }

  END_TEST;
}

UNITTEST_START_TESTCASE(heap_tests)
UNITTEST("test allocations are zeroed if alloc_fill_threshold is set", test_alloc_fill_threshold)
UNITTEST_END_TESTCASE(heap_tests, "heap", "heap tests")
