// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/instrumentation/asan.h>
#include <lib/unittest/unittest.h>
#include <platform.h>
#include <zircon/types.h>

#include <new>

#include <fbl/alloc_checker.h>
#include <kernel/thread.h>
#include <kernel/wait.h>
#include <ktl/array.h>
#include <ktl/limits.h>
#include <ktl/unique_ptr.h>
#include <vm/physmap.h>
#include <vm/pmm.h>

#if __has_feature(address_sanitizer)
namespace {

constexpr size_t kAsanShift = 3;

// Makes sure that a regions from the heap can be poisoned.
bool kasan_test_poison_heap() {
  BEGIN_TEST;

  constexpr size_t sizes[] = {1, 2, 3, 5, 7, 8, 9, 11, 15, 16, 17, 19};

  constexpr size_t kBufSz = 1024;
  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[kBufSz]);
  ASSERT(ac.check());
  ASSERT_EQ(0UL, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), kBufSz));
  for (size_t size : sizes) {
    size_t poisoned_size = ROUNDDOWN(size, 1 << kAsanShift);
    asan_poison_shadow(reinterpret_cast<uintptr_t>(buf.get()), size, kAsanRedZone);
    EXPECT_TRUE(
        asan_entire_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), poisoned_size));
    asan_poison_shadow(reinterpret_cast<uintptr_t>(buf.get()), kBufSz, 0);
    EXPECT_EQ(0UL, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), kBufSz));
  }

  END_TEST;
}

// Make sure poison checks works in partially poisoned regions.
bool kasan_test_poison_heap_partial() {
  BEGIN_TEST;

  constexpr size_t kBufSz = 128;
  fbl::AllocChecker ac;
  auto buf = ktl::unique_ptr<uint8_t[]>(new (&ac) uint8_t[kBufSz]);
  ASSERT(ac.check());
  ASSERT_EQ(0UL, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), kBufSz));

  // Leave the first and last two granules unpoisoned.
  const uintptr_t poison_start = reinterpret_cast<uintptr_t>(buf.get()) + (2 << kAsanShift);
  const size_t poison_size = kBufSz - (4 << kAsanShift);

  asan_poison_shadow(poison_start, poison_size, kAsanRedZone);
  EXPECT_EQ(poison_start, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), kBufSz));

  asan_poison_shadow(poison_start, poison_size, 0);
  ASSERT_EQ(0UL, asan_region_is_poisoned(reinterpret_cast<uintptr_t>(buf.get()), kBufSz));

  END_TEST;
}

}  // namespace

UNITTEST_START_TESTCASE(kasan_tests)
UNITTEST("test_poisoning_heap", kasan_test_poison_heap)
UNITTEST("test_poisoning_heap_partial", kasan_test_poison_heap_partial)
UNITTEST_END_TESTCASE(kasan_tests, "kasan", "Kernel Address Sanitizer Tests")

#endif  // _has_feature(address_sanitizer)
