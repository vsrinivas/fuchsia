// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/cache.h>
#include <lib/unittest/unittest.h>

#include <fbl/ref_ptr.h>

namespace {

// The following tests exercise the architecture-specific cache cleaning
// and invalidation functions.
//
// We don't attempt to ensure the functions actually modify the cache in
// the correct manner, but rather just call the functions and make sure
// the system doesn't catch on fire.

bool test_clean_cache() {
  BEGIN_TEST;

  arch::CleanLocalCaches();

  END_TEST;
}

bool test_clean_invalidate_cache() {
  BEGIN_TEST;

  arch::CleanAndInvalidateLocalCaches();

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(arm64_cache_test)
UNITTEST("test_clean_cache", test_clean_cache)
UNITTEST("test_clean_invalidate_cache", test_clean_invalidate_cache)
UNITTEST_END_TESTCASE(arm64_cache_test, "arm64_cache", "Tests exercising ARM64 cache operations.")
