// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/internal.h>
#include <lib/user_copy/user_ptr.h>
#include <zircon/syscalls/port.h>

#include <vm/fault.h>
#include <vm/vm_aspace.h>
#include <vm/vm_object_paged.h>

namespace {

using testing::UserMemory;

constexpr uint32_t kTestValue = 0xDEADBEEF;

bool test_copy_out(bool pre_map) {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);

  if (pre_map) {
    ASSERT_EQ(user->CommitAndMap(PAGE_SIZE), ZX_OK, "");
  }

  ASSERT_EQ(user->user_out<uint32_t>().copy_to_user(kTestValue), ZX_OK, "");

  uint32_t temp;
  ASSERT_EQ(user->VmoRead(&temp, 0, sizeof(temp)), ZX_OK, "");
  EXPECT_EQ(temp, kTestValue, "");

  END_TEST;
}

bool test_copy_in(bool pre_map) {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);

  if (pre_map) {
    ASSERT_EQ(user->CommitAndMap(PAGE_SIZE), ZX_OK, "");
  }

  ASSERT_EQ(user->VmoWrite(&kTestValue, 0, sizeof(kTestValue)), ZX_OK, "");

  uint32_t temp;
  ASSERT_EQ(user->user_in<uint32_t>().copy_from_user(&temp), ZX_OK, "");

  EXPECT_EQ(temp, kTestValue, "");

  END_TEST;
}

bool pre_map_copy_out() { return test_copy_out(true); }

bool fault_copy_out() { return test_copy_out(false); }

bool pre_map_copy_in() { return test_copy_in(true); }

bool fault_copy_in() { return test_copy_in(false); }

bool capture_faults_copy_out_success() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);
  ASSERT_EQ(user->CommitAndMap(PAGE_SIZE), ZX_OK, "");

  vaddr_t pf_va;
  uint pf_flags;
  ASSERT_EQ(user->user_out<uint32_t>().copy_to_user_capture_faults(kTestValue, &pf_va, &pf_flags),
            ZX_OK, "");

  uint32_t temp;
  ASSERT_EQ(user->VmoRead(&temp, 0, sizeof(temp)), ZX_OK, "");
  EXPECT_EQ(temp, kTestValue, "");

  END_TEST;
}

bool capture_faults_copy_in_success() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);
  ASSERT_EQ(user->CommitAndMap(PAGE_SIZE), ZX_OK, "");

  ASSERT_EQ(user->VmoWrite(&kTestValue, 0, sizeof(kTestValue)), ZX_OK, "");

  vaddr_t pf_va;
  uint pf_flags;
  uint32_t temp;
  ASSERT_EQ(user->user_in<uint32_t>().copy_from_user_capture_faults(&temp, &pf_va, &pf_flags),
            ZX_OK, "");

  EXPECT_EQ(temp, kTestValue, "");

  END_TEST;
}

bool capture_faults_test_capture() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);

  vaddr_t pf_va;
  uint pf_flags;
  uint32_t temp;
  ASSERT_NE(user->user_in<uint32_t>().copy_from_user_capture_faults(&temp, &pf_va, &pf_flags),
            ZX_OK, "");
  EXPECT_EQ(pf_va, user->base(), "");
  EXPECT_EQ(pf_flags, VMM_PF_FLAG_NOT_PRESENT, "");

  ASSERT_NE(user->user_out<uint32_t>().copy_to_user_capture_faults(kTestValue, &pf_va, &pf_flags),
            ZX_OK, "");
  EXPECT_EQ(pf_va, user->base(), "");
  EXPECT_EQ(pf_flags, VMM_PF_FLAG_NOT_PRESENT | VMM_PF_FLAG_WRITE, "");

  END_TEST;
}

// Verify is_copy_allowed<T>::value is true when T contains no implicit padding.
struct SomeTypeWithNoPadding {
  uint64_t field1;
};
static_assert(internal::is_copy_allowed<SomeTypeWithNoPadding>::value);
static_assert(internal::is_copy_allowed<int>::value);
static_assert(internal::is_copy_allowed<zx_port_packet_t>::value);

// Verify is_copy_allowed<void>::value is false.
static_assert(!internal::is_copy_allowed<void>::value);

// Verify is_copy_allowed<T>::value is false when T contains implicit padding.
struct SomeTypeWithPadding {
  uint64_t field1;
  uint32_t field2;
};
static_assert(!internal::is_copy_allowed<SomeTypeWithPadding>::value);

// Verify is_copy_allowed<T>::value is false when T does not have a standard-layout.
struct SomeTypeWithNonStandardLayout : SomeTypeWithNoPadding {
  uint32_t another_field;
};
static_assert(!internal::is_copy_allowed<SomeTypeWithNonStandardLayout>::value);

// Verify is_copy_allowed<T>::value is false when T is not trival.
struct SomeTypeNonTrivial {
  SomeTypeNonTrivial(const SomeTypeNonTrivial& other) { another_field = other.another_field; }
  uint32_t another_field;
};
static_assert(!internal::is_copy_allowed<SomeTypeNonTrivial>::value);

}  // namespace

#define USER_COPY_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(user_copy_tests)
USER_COPY_UNITTEST(pre_map_copy_out)
USER_COPY_UNITTEST(fault_copy_out)
USER_COPY_UNITTEST(pre_map_copy_in)
USER_COPY_UNITTEST(fault_copy_in)
USER_COPY_UNITTEST(capture_faults_copy_out_success)
USER_COPY_UNITTEST(capture_faults_copy_in_success)
USER_COPY_UNITTEST(capture_faults_test_capture)
UNITTEST_END_TESTCASE(user_copy_tests, "user_copy_tests", "User Copy test");
