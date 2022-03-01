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
#include <lib/zircon-internal/macros.h>
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

bool test_addresses_outside_user_range(bool capture_faults) {
  BEGIN_TEST;

  // User copy routines can operate on user addresses whose values may be outside
  // the range of [USER_ASPACE_BASE, USER_ASPACE_BASE + USER_ASPACE_SIZE]. If a
  // user_copy function accepts an address that userspace would normally fault on
  // when accessed, then user_copy will page fault on that address.
  //
  // Test to make sure that we fault on anything that userspace would normally
  // fault on. If we do fault or receive something that isn't accessible to the
  // user (determined by the arch), then ZX_ERR_INVALID_ARGS is returned. If
  // there was a fault, then fault info should be provided.
  uint8_t test_buffer[32] = {0};
  static_assert((sizeof(test_buffer) * 2) < USER_ASPACE_BASE,
                "Insufficient space before start of user address space for test buffer");
  static_assert((sizeof(test_buffer) * 2) <
                    (ktl::numeric_limits<vaddr_t>::max() - (USER_ASPACE_BASE + USER_ASPACE_SIZE)),
                "Insufficient space afer end of user address space for test buffer.");
  struct TestCase {
    vaddr_t test_addr;
    zx_status_t copy_from_user_expected_status;
    zx_status_t copy_to_user_expected_status;

    // These are for differentiating between failed copies. user_copy would
    // fail if the `test_addr` is an address the user wouldn't be able to
    // access, but wouldn't fault. Whereas other user addresses would fail on
    // user_copy from faults.
    bool copy_from_user_expected_fault;
    bool copy_to_user_expected_fault;
  };
  constexpr TestCase kTestCases[] = {
    // These addresses will result in ZX_ERR_INVALID_ARGS when copying to and
    // from a user pointer because we fault on bad addresses.

    // Explicit check of null
    {static_cast<vaddr_t>(0), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, true, true},
    // Entirely before USER_ASPACE_BASE
    {USER_ASPACE_BASE - (sizeof(test_buffer) * 2), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, true, true},
    // Overlapping USER_ASPACE_BASE
    {USER_ASPACE_BASE - (sizeof(test_buffer) / 2), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, true, true},

#if defined(__aarch64__)

    // These addresses will result in ZX_ERR_INVALID_ARGS when copying to a
    // user pointer because we fault on a bad address.
    //
    // FIXME: ZX_OK is returned here when copying *from* a user pointer because
    // the `copy_from_user` functions have spectre mitigation that sets the
    // source address and length to zero if any part of the address range
    // provided is exceeds USER_ASPACE_BASE + USER_ASPACE_SIZE (see
    // `internal::confine_user_address_range`). This means copies on user
    // addresses that exceed USER_ASPACE_BASE + USER_ASPACE_SIZE will silently
    // pass without an error or performing the actual copy.

    // Entirely after USER_ASPACE_BASE + USER_ASPACE_SIZE
    {USER_ASPACE_BASE + USER_ASPACE_SIZE + sizeof(test_buffer), ZX_OK, ZX_ERR_INVALID_ARGS, false, true},
    // Overlapping USER_ASPACE_BASE + USER_ASPACE_SIZE
    {USER_ASPACE_BASE + USER_ASPACE_SIZE - (sizeof(test_buffer) / 2), ZX_OK, ZX_ERR_INVALID_ARGS, false, true},

    // On AArch64, an address is considered accessible to the user if bit 55
    // is zero. This implies addresses above 2^55 that don't set that bit are
    // considered accessible to the user, but the user_copy operation would
    // still fault on them.
    //
    // These addresses will result in ZX_ERR_INVALID_ARGS when copying to and
    // from a user pointer either because bit 55 is not zero or there was a
    // page fault.

    // Start at 2^55
    {UINT64_C(1) << 55, ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, false, false},
    // Slightly after 2^55 (bit 55 is set)
    {(UINT64_C(1) << 55) + sizeof(test_buffer), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, false, false},
    // Overlapping 2^55
    {(UINT64_C(1) << 55) - sizeof(test_buffer) / 2, ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, false, false},

    // These addresses will result in ZX_ERR_INVALID_ARGS when copying to a
    // user pointer because we fault on a bad address.
    //
    // FIXME: These return ZX_OK when copying from a user pointer for the
    // same reason as above.

    // End right before 2^55
    {(UINT64_C(1) << 55) - sizeof(test_buffer), ZX_OK, ZX_ERR_INVALID_ARGS, false, true},
    // Way beyond 2^55 (bit 55 is not set)
    {(UINT64_C(1) << 56), ZX_OK, ZX_ERR_INVALID_ARGS, false, true},

#elif defined(__x86_64__)

    // On x86_64, an address is considered accessible to the user if everything
    // above bit 47 is zero. Passing an address that doesn't meet this constraint
    // will cause the user_copy to fail without performing the copy.
    //
    // NOTE: On x86 this doesn't succumb to the issue mentioned above with spectre
    // mitigation because the x86 user_copy implementation doesn't call
    // `internal::confine_user_address_range`.

    // Entirely after USER_ASPACE_BASE + USER_ASPACE_SIZE
    {USER_ASPACE_BASE + USER_ASPACE_SIZE + sizeof(test_buffer), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, true, true},
    // Overlapping USER_ASPACE_BASE + USER_ASPACE_SIZE
    {USER_ASPACE_BASE + USER_ASPACE_SIZE - (sizeof(test_buffer) / 2), ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, true, true},

    // Start at 2^48
    {UINT64_C(1) << 48, ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, false, false},
    // Overlapping 2^48
    {(UINT64_C(1) << 48) - sizeof(test_buffer) / 2, ZX_ERR_INVALID_ARGS, ZX_ERR_INVALID_ARGS, false, false},

#endif

  };

  for (const TestCase& test_case : kTestCases) {
    vaddr_t test_addr = test_case.test_addr;
    zx_status_t copy_from_user_expected_status = test_case.copy_from_user_expected_status;
    zx_status_t copy_to_user_expected_status = test_case.copy_to_user_expected_status;
    bool copy_from_user_expected_fault = test_case.copy_from_user_expected_fault;
    bool copy_to_user_expected_fault = test_case.copy_to_user_expected_fault;
    printf("test_addr: 0x%lx\n", test_addr);

    {
      user_in_ptr<const uint8_t> user{reinterpret_cast<uint8_t*>(test_addr)};

      if (capture_faults) {
        auto ret = user.copy_array_from_user_capture_faults(test_buffer, std::size(test_buffer), 0);
        EXPECT_EQ(copy_from_user_expected_status, ret.status);
        if (ret.status == ZX_OK) {
          EXPECT_FALSE(ret.fault_info.has_value());
        }
        if (copy_from_user_expected_fault) {
          EXPECT_TRUE(ret.fault_info.has_value());
          EXPECT_EQ(ret.fault_info->pf_va, test_addr, "Page faulted on the user address");
        }
      } else {
        auto ret = user.copy_array_from_user(test_buffer, std::size(test_buffer));
        EXPECT_EQ(copy_from_user_expected_status, ret);
      }
    }

    {
      user_out_ptr<uint8_t> user{reinterpret_cast<uint8_t*>(test_addr)};

      if (capture_faults) {
        auto ret = user.copy_array_to_user_capture_faults(test_buffer, std::size(test_buffer), 0);
        EXPECT_EQ(copy_to_user_expected_status, ret.status);
        if (ret.status == ZX_OK) {
          EXPECT_FALSE(ret.fault_info.has_value());
        }
        if (copy_to_user_expected_fault) {
          EXPECT_TRUE(ret.fault_info.has_value());
          EXPECT_EQ(ret.fault_info->pf_va, test_addr, "Page faulted on the user address");
        }
      } else {
        auto ret = user.copy_array_to_user(test_buffer, std::size(test_buffer));
        EXPECT_EQ(copy_to_user_expected_status, ret);
      }
    }
  }

  END_TEST;
}

bool user_copy_test_addresses_outside_user_range() {
  return test_addresses_outside_user_range(false);
}
bool capture_faults_test_addresses_outside_user_range() {
  return test_addresses_outside_user_range(true);
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
  ASSERT_TRUE(user_iovec);
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

bool test_confine_user_address_range() {
  BEGIN_TEST;
  vaddr_t va;
  size_t len;

  // Test that accessing the last byte of an address space is allowed.
  va = 0xfffu;
  len = 1;
  internal::confine_user_address_range(&va, &len, 4 * KB);
  EXPECT_EQ(0xfffu, va);
  EXPECT_EQ(1u, len);

  // Check that the last byte of an address space plus first outside don't work.
  va = 0xfff;
  len = 2;
  internal::confine_user_address_range(&va, &len, 4 * KB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(0u, len);

  // Test that a vaddr that starts outside the address space doesn't work.
  va = 0x1fffu;
  len = 1024u;
  internal::confine_user_address_range(&va, &len, 4 * KB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(0u, len);

  // Test that accessing an entire small space works.
  va = 0u;
  len = 4 * KB;
  internal::confine_user_address_range(&va, &len, 4 * KB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(4 * KB, len);

  // Test that a 4 GB (full width of uint32_t) range is allowed.
  va = 0x10000u;
  len = 4 * MB;
  internal::confine_user_address_range(&va, &len, 4 * GB);
  EXPECT_EQ(0x10000u, va);
  EXPECT_EQ(4 * MB, len);

  // Test that length > top doesn't allow work.
  va = 0x10000u;
  len = 4 * GB * GB;
  internal::confine_user_address_range(&va, &len, 4 * GB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(0u, len);

  // Test that uint64_t wraparound for address doesn't allow access.
  va = -1ul;
  len = 2;
  internal::confine_user_address_range(&va, &len, 4 * GB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(0u, len);

  // Test that uint64_t wraparound for len doesn't allow access.
  va = 0;
  len = -1;
  internal::confine_user_address_range(&va, &len, 4 * GB);
  EXPECT_EQ(0u, va);
  EXPECT_EQ(0u, len);

  END_TEST;
}

}  // namespace

#define USER_COPY_UNITTEST(fname) UNITTEST(#fname, fname)

UNITTEST_START_TESTCASE(user_copy_tests)
USER_COPY_UNITTEST(pre_map_copy_out)
USER_COPY_UNITTEST(fault_copy_out)
USER_COPY_UNITTEST(pre_map_copy_in)
USER_COPY_UNITTEST(fault_copy_in)
USER_COPY_UNITTEST(user_copy_test_addresses_outside_user_range)
USER_COPY_UNITTEST(capture_faults_copy_out_success)
USER_COPY_UNITTEST(capture_faults_copy_in_success)
USER_COPY_UNITTEST(capture_faults_test_capture)
USER_COPY_UNITTEST(capture_faults_test_addresses_outside_user_range)
USER_COPY_UNITTEST(test_get_total_capacity)
USER_COPY_UNITTEST(test_iovec_foreach)
USER_COPY_UNITTEST(test_confine_user_address_range)
UNITTEST_END_TESTCASE(user_copy_tests, "user_copy_tests", "User Copy test")
