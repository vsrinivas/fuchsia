// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "asid_allocator.h"

#include <debug.h>
#include <lib/unittest/unittest.h>
#include <trace.h>
#include <zircon/types.h>

#include <ktl/unique_ptr.h>

#include <ktl/enforce.h>

#define LOCAL_TRACE 0

AsidAllocator::AsidAllocator(enum arm64_asid_width width_override) {
  bitmap_.Reset(MMU_ARM64_MAX_USER_ASID_16 + 1);

  // save whether or not the cpu only supports 8 bits which is fairly exceptional.
  // most support the full 16 bit ASID space.
  asid_width_ = (width_override != arm64_asid_width::UNKNOWN) ? width_override : arm64_asid_width();
  DEBUG_ASSERT(asid_width_ == arm64_asid_width::ASID_8 || asid_width_ == arm64_asid_width::ASID_16);
}

AsidAllocator::~AsidAllocator() {}

zx::result<uint16_t> AsidAllocator::Alloc() {
  uint16_t new_asid;

  // use the bitmap allocator to allocate ids in the range of
  // [MMU_ARM64_FIRST_USER_ASID, MMU_ARM64_MAX_USER_ASID]
  // start the search from the last found id + 1 and wrap when hitting the end of the range
  {
    Guard<Mutex> al{&lock_};

    size_t val;
    bool notfound = bitmap_.Get(last_ + 1, max_user_asid() + 1, &val);
    if (unlikely(notfound)) {
      // search again from the start
      notfound = bitmap_.Get(MMU_ARM64_FIRST_USER_ASID, max_user_asid() + 1, &val);
      if (unlikely(notfound)) {
        return zx::error(ZX_ERR_NO_MEMORY);
      }
    }
    bitmap_.SetOne(val);

    DEBUG_ASSERT(val <= max_user_asid());

    new_asid = (uint16_t)val;
    last_ = new_asid;
  }

  LTRACEF("new asid %#x\n", new_asid);

  return zx::ok(new_asid);
}

zx::result<> AsidAllocator::Free(uint16_t asid) {
  LTRACEF("free asid %#x\n", asid);

  Guard<Mutex> al{&lock_};

  bitmap_.ClearOne(asid);

  return zx::ok();
}

// unit tests for the asid allocator
namespace {

bool asid_allocator_test_inner(enum arm64_asid_width asid_width) {
  BEGIN_TEST;

  fbl::AllocChecker ac;
  ktl::unique_ptr<AsidAllocator> aa(new (&ac) AsidAllocator(asid_width));
  ASSERT_TRUE(ac.check());

  // test that it computed the correct asid width
  uint32_t max_asid = (asid_width == arm64_asid_width::ASID_8) ? MMU_ARM64_MAX_USER_ASID_8
                                                               : MMU_ARM64_MAX_USER_ASID_16;
  ASSERT_EQ(aa->max_user_asid(), max_asid);

  // run the test twice to make sure it clears back to a default state
  for (auto j = 0; j < 2; j++) {
    // use up all the asids
    for (uint32_t i = MMU_ARM64_FIRST_USER_ASID; i <= max_asid; i++) {
      auto status = aa->Alloc();
      ASSERT_TRUE(status.is_ok());

      ASSERT_GE(status.value(), MMU_ARM64_FIRST_USER_ASID);
      ASSERT_LE(status.value(), max_asid);
    }

    // expect the next one to fail
    {
      auto status = aa->Alloc();
      EXPECT_TRUE(status.is_error());
      EXPECT_EQ(status.status_value(), ZX_ERR_NO_MEMORY);
    }

    // free them all
    for (uint32_t i = MMU_ARM64_FIRST_USER_ASID; i <= max_asid; i++) {
      auto status = aa->Free(static_cast<uint16_t>(i));
      ASSERT_TRUE(status.is_ok());
    }
  }

  END_TEST;
}

bool asid_allocator_test_8bit() { return asid_allocator_test_inner(arm64_asid_width::ASID_8); }

bool asid_allocator_test_16bit() { return asid_allocator_test_inner(arm64_asid_width::ASID_16); }

UNITTEST_START_TESTCASE(asid_allocator)
UNITTEST("8 bit asid allocator test", asid_allocator_test_8bit)
UNITTEST("16 bit asid allocator test", asid_allocator_test_16bit)
UNITTEST_END_TESTCASE(asid_allocator, "asid", "Tests for asid allocator")

}  // anonymous namespace
