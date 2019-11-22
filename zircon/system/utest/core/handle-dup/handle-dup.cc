// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/syscalls/object.h>
#include <fbl/vector.h>
#include <lib/zx/event.h>
#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kEventOption = 0u;

TEST(HandleDup, ReplaceSuccessOrigInvalid) {
  zx::event orig_event;
  ASSERT_OK(zx::event::create(kEventOption, &orig_event));

  zx::event replaced_event;
  ASSERT_OK(orig_event.replace(ZX_RIGHTS_BASIC, &replaced_event));
  EXPECT_FALSE(orig_event.is_valid());
  EXPECT_TRUE(replaced_event.is_valid());
}

TEST(HandleDup, ReplaceFailureBothInvalid) {
  zx::event orig_event;
  ASSERT_OK(zx::event::create(kEventOption, &orig_event));

  zx::event failed_event;
  EXPECT_EQ(orig_event.replace(ZX_RIGHT_EXECUTE, &failed_event), ZX_ERR_INVALID_ARGS);
  // Even on failure, a replaced object is now invalid.
  EXPECT_FALSE(orig_event.is_valid());
  EXPECT_FALSE(failed_event.is_valid());
}

// UBSan triggers on nullptrs passed as an argument, but these tests are meant
// to test the kernel's handling of an invalid argument, which we want. So we
// just disable UBSan for these two tests.
#ifdef __clang__
[[clang::no_sanitize("undefined")]]
#endif
void TestReplace() {
  // Call handle_replace with an invalid destination slot. This will cause the handle to get
  // duplicated in the kernel, but then have to get deleted at the point the copy-out happens.
  zx::event event;

  ASSERT_OK(zx::event::create(kEventOption, &event));

  // This should fail and not cause the kernel to panic.
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, zx_handle_replace(event.get(), 0, nullptr));
}

TEST(HandleDup, Replace) {
  ASSERT_NO_FATAL_FAILURES(TestReplace());
}

#ifdef __clang__
[[clang::no_sanitize("undefined")]]
#endif
void TestDuplicate() {
  // Same as above, but using the handle_duplicate to cause the dup to happen in the kernel.
  zx::event event;

  ASSERT_OK(zx::event::create(kEventOption, &event));

  // This should fail and not cause the kernel to panic.
  ASSERT_STATUS(ZX_ERR_INVALID_ARGS, zx_handle_duplicate(event.get(), 0, nullptr));
}

TEST(HandleDup, Duplicate) {
  ASSERT_NO_FATAL_FAILURES(TestDuplicate());
}

}  // namespace
