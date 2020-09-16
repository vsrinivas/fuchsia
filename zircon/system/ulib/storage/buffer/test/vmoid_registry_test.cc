// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "storage/buffer/vmoid_registry.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace storage {
namespace {

using ::testing::_;

TEST(VmoidTest, Move) {
  Vmoid vmoid(1), vmoid2;
  vmoid2 = std::move(vmoid);
  EXPECT_FALSE(vmoid.IsAttached());
  EXPECT_TRUE(vmoid2.IsAttached());
  [[maybe_unused]] vmoid_t id = vmoid2.TakeId();
}

TEST(VmoidDeathTest, ForgottenDetachAssertsInDebug) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  ASSERT_DEATH({ Vmoid vmoid(1); }, _);
#else
  Vmoid vmoid(1);
#endif
}

TEST(VmoidDeathTest, MoveToAttachedVmoidAssertsInDebug) {
#if ZX_DEBUG_ASSERT_IMPLEMENTED
  ASSERT_DEATH(
      {
        Vmoid vmoid(1);
        Vmoid vmoid2(2);
        vmoid = std::move(vmoid2);
      },
      _);
#else
  Vmoid vmoid(1), vmoid2(2);
  vmoid = std::move(vmoid2);
#endif
}

}  // namespace
}  // namespace storage
