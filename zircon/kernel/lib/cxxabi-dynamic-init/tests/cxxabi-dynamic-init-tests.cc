// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/cxxabi-dynamic-init/cxxabi-dynamic-init.h>
#include <lib/fit/defer.h>

#include <zxtest/zxtest.h>

using cxxabi_dynamic_init::Abort;
using cxxabi_dynamic_init::Acquire;
using cxxabi_dynamic_init::Release;

// Provide a fake implementation of ConstructorsCalled so that the tests can simulate pre and post
// constructor context.
bool gCtorsCalled = false;
namespace cxxabi_dynamic_init::internal {
bool ConstructorsCalled() { return gCtorsCalled; }
}  // namespace cxxabi_dynamic_init::internal

// Verify behavior of acquire when variable is already initialized.
TEST(StaticInitGuardTest, AcquireAlreadyInitialized) {
  uint64_t guard = 1;
  // Failed to acquire because object is already initialized.
  ASSERT_EQ(0, Acquire(&guard));
  // Guard is unchanged.
  ASSERT_EQ(1, guard);
}

// Verify happy case of acquire then release.
TEST(StaticInitGuardTest, AcquireRelease) {
  uint64_t guard = 0;
  ASSERT_EQ(1, Acquire(&guard));
  Release(&guard);
  // Guard shows initialized.
  ASSERT_EQ(1, 0x000000ff & guard);
}

// Verify that an aborted initialization can be retried.
TEST(StaticInitGuardTest, AcquireAbort) {
  uint64_t guard = 0;
  ASSERT_EQ(1, Acquire(&guard));
  Abort(&guard);
  // Guard shows uninitialized.
  ASSERT_EQ(0, guard);

  // Try again.
  ASSERT_EQ(1, Acquire(&guard));
  Release(&guard);
  // Guard shows initialized.
  ASSERT_EQ(1, 0x000000ff & guard);
}

// Skip the following tests if run in the host environment, which does not have ASSERT_DEATH, or if
// ZX_DEBUG_ASSERTs aren't implemented.
#if defined(__Fuchsia__) && ZX_DEBUG_ASSERT_IMPLEMENTED

// Verify that attempting to acquire an already acquired guard results in a debug assert.
TEST(StaticInitGuardTest, DoubleAcquireMaybeDeath) {
  uint64_t guard = 0;
  ASSERT_EQ(1, Acquire(&guard));
  ASSERT_DEATH([&]() { Acquire(&guard); });
}

// Verify that attempting to acquire after global ctors have been called results in a debug assert.
TEST(StaticInitGuardTest, AcquireAfterGlobalCtorsMaybeDeath) {
  gCtorsCalled = true;
  auto cleanup = fit::defer([]() { gCtorsCalled = false; });
  uint64_t guard = 0;
  ASSERT_DEATH([&]() { Acquire(&guard); });
}

// Verify that attempting to release after global ctors have been called results in a debug assert.
TEST(StaticInitGuardTest, ReleaseAfterGlobalCtorsMaybeDeath) {
  uint64_t guard = 0;
  ASSERT_EQ(1, Acquire(&guard));

  gCtorsCalled = true;
  auto cleanup = fit::defer([]() { gCtorsCalled = false; });
  ASSERT_DEATH([&]() { Release(&guard); });
}

// Verify that attempting to abort after global ctors have been called results in a debug assert.
TEST(StaticInitGuardTest, AbortAfterGlobalCtorsMaybeDeath) {
  uint64_t guard = 0;
  ASSERT_EQ(1, Acquire(&guard));

  gCtorsCalled = true;
  auto cleanup = fit::defer([]() { gCtorsCalled = false; });
  ASSERT_DEATH([&]() { Abort(&guard); });
}

#endif
