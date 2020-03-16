// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/unittest/unittest.h>
#include <lib/unittest/user_memory.h>
#include <lib/user_copy/internal.h>
#include <lib/user_copy/user_iovec.h>
#include <lib/user_copy/user_ptr.h>
#include <zircon/syscalls/port.h>

#include <ktl/array.h>
#include <ktl/limits.h>
#include <ktl/move.h>
#include <ktl/unique_ptr.h>
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

  auto ret = user->user_out<uint32_t>().copy_to_user_capture_faults(kTestValue);
  ASSERT_FALSE(ret.fault_info.has_value(), "");
  ASSERT_EQ(ZX_OK, ret.status, "");

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

  uint32_t temp;
  auto ret = user->user_in<uint32_t>().copy_from_user_capture_faults(&temp);
  ASSERT_FALSE(ret.fault_info.has_value(), "");
  ASSERT_EQ(ZX_OK, ret.status, "");

  EXPECT_EQ(temp, kTestValue, "");

  END_TEST;
}

bool capture_faults_test_capture() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);
  uint32_t temp;

  {
    auto ret = user->user_in<uint32_t>().copy_from_user_capture_faults(&temp);
    ASSERT_TRUE(ret.fault_info.has_value(), "");
    ASSERT_NE(ZX_OK, ret.status);

    const auto& fault_info = ret.fault_info.value();
    EXPECT_EQ(fault_info.pf_va, user->base(), "");
    EXPECT_EQ(fault_info.pf_flags, VMM_PF_FLAG_NOT_PRESENT, "");
  }

  {
    auto ret = user->user_out<uint32_t>().copy_to_user_capture_faults(kTestValue);
    ASSERT_TRUE(ret.fault_info.has_value(), "");
    ASSERT_NE(ZX_OK, ret.status);

    const auto& fault_info = ret.fault_info.value();
    EXPECT_EQ(fault_info.pf_va, user->base(), "");
    EXPECT_EQ(fault_info.pf_flags, VMM_PF_FLAG_NOT_PRESENT | VMM_PF_FLAG_WRITE, "");
  }

  END_TEST;
}

bool capture_faults_test_out_of_user_range() {
  BEGIN_TEST;

  // If a user copy routine is asked to copy data to/from addresses which cannot
  // possibly exist in the user's address space, the copy routine will not even
  // try to perform the copy.  There is no "fault address" in this case, but the
  // operation is still a failure.
  //
  // Test to make sure that we catch and reject anything which is outside of or
  // overlapping with the acceptable user address space range.  None of these
  // requests should be attempted, nor should they produce fault info.
  uint8_t test_buffer[32] = {0};
  static_assert((sizeof(test_buffer) * 2) < USER_ASPACE_BASE,
                "Insufficient space before start of user address space for test buffer");
  static_assert((sizeof(test_buffer) * 2) <
                    (ktl::numeric_limits<vaddr_t>::max() - (USER_ASPACE_BASE + USER_ASPACE_SIZE)),
                "Insufficient space afer end of user address space for test buffer.");
  constexpr ktl::array<vaddr_t, 5> kTestAddrs = {
      static_cast<vaddr_t>(0),                                          // Explicit check of null
      USER_ASPACE_BASE - (sizeof(test_buffer) * 2),                     // Entirely before
      USER_ASPACE_BASE - (sizeof(test_buffer) / 2),                     // Overlapping start
      USER_ASPACE_BASE + USER_ASPACE_SIZE + sizeof(test_buffer),        // Entirely after
      USER_ASPACE_BASE + USER_ASPACE_SIZE - (sizeof(test_buffer) / 2),  // Overlapping end
  };

  for (const vaddr_t test_addr : kTestAddrs) {
    ASSERT_FALSE(is_user_address_range(test_addr, sizeof(test_buffer)));

    {
      user_in_ptr<const uint8_t> user{reinterpret_cast<uint8_t*>(test_addr)};

      auto ret = user.copy_array_from_user_capture_faults(test_buffer, countof(test_buffer), 0);
      ASSERT_FALSE(ret.fault_info.has_value(), "");
      EXPECT_EQ(ZX_ERR_INVALID_ARGS, ret.status);
    }

    {
      user_out_ptr<uint8_t> user{reinterpret_cast<uint8_t*>(test_addr)};

      auto ret = user.copy_array_to_user_capture_faults(test_buffer, countof(test_buffer), 0);
      ASSERT_FALSE(ret.fault_info.has_value(), "");
      EXPECT_EQ(ZX_ERR_INVALID_ARGS, ret.status);
    }
  }

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

bool test_get_total_capacity() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);

  zx_iovec_t vec[2] = {};
  vec[0].capacity = 348u;
  vec[1].capacity = 58u;

  ASSERT_EQ(user->VmoWrite(vec, 0, sizeof(vec)), ZX_OK, "");

  user_in_iovec_t user_iovec = make_user_in_iovec(user->user_in<zx_iovec_t>(), 2);
  size_t total_capacity = 97u;
  ASSERT_EQ(user_iovec.GetTotalCapacity(&total_capacity), ZX_OK, "");
  ASSERT_EQ(total_capacity, 406u);

  END_TEST;
}

// This class exists to have an overloaded operator(). In particular, the two overloads (by constant
// reference, and by rvalue reference) help detect whether this callable object is forwarded
// multiple times.
class Multiply {
 public:
  explicit Multiply(size_t* value) : value_(value) {}

  // Call this not via an rvalue reference.
  zx_status_t operator()(user_in_ptr<const char> unused, size_t value) const& {
    *value_ *= value;
    return ZX_ERR_NEXT;
  }

  // Call this via an rvalue reference.
  zx_status_t operator()(user_in_ptr<const char> unused, size_t value) && {
    return ZX_ERR_BAD_STATE;
  }

 private:
  mutable size_t* value_;
};

bool test_iovec_foreach() {
  BEGIN_TEST;

  auto user = UserMemory::Create(PAGE_SIZE);

  zx_iovec_t vec[3] = {};
  vec[0].capacity = 7u;
  vec[1].capacity = 11u;
  vec[2].capacity = 13u;

  ASSERT_EQ(user->VmoWrite(vec, 0, sizeof(vec)), ZX_OK, "");

  user_in_iovec_t user_iovec = make_user_in_iovec(user->user_in<zx_iovec_t>(), 3);

  size_t product = 2u;
  Multiply multiply{&product};
  // It is important that this Multiply object is moved into the ForEach call. By doing so, combined
  // with the operator() overload set, we can determine whether ForEach invokes us by rvalue or
  // not. In particular, ktl::forward()ing a ktl::move()d Multiply would fail.
  ASSERT_EQ(user_iovec.ForEach(ktl::move(multiply)), ZX_OK, "");
  ASSERT_EQ(product, 2002u);

  END_TEST;
}

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
USER_COPY_UNITTEST(capture_faults_test_out_of_user_range)
USER_COPY_UNITTEST(test_get_total_capacity)
USER_COPY_UNITTEST(test_iovec_foreach)
UNITTEST_END_TESTCASE(user_copy_tests, "user_copy_tests", "User Copy test")
