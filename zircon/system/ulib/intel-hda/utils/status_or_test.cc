// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-hda/utils/status_or.h"

#include <zircon/errors.h>

#include <string>

#include <zxtest/zxtest.h>

#include "fbl/alloc_checker.h"

namespace audio::intel_hda {
namespace {

TEST(StatusOr, DefaultConstructed) {
  StatusOr<int> s;
  EXPECT_FALSE(s.ok());
  EXPECT_EQ(s.status().code(), ZX_ERR_INTERNAL);
}

TEST(StatusOr, Value) {
  StatusOr<int> s{3};
  EXPECT_TRUE(s.ok());
  EXPECT_TRUE(s.status().ok());
  EXPECT_EQ(3, s.ValueOrDie());
}

StatusOr<int> Return3() { return 3; }

TEST(StatusOr, ImplicitValueConversion) { EXPECT_EQ(Return3().ValueOrDie(), 3); }

StatusOr<int> ReturnError() { return Status{ZX_ERR_ACCESS_DENIED, "no entry"}; }

TEST(StatusOr, ImplicitStatusConversion) {
  StatusOr<int> result = ReturnError();
  EXPECT_EQ(result.status().code(), ZX_ERR_ACCESS_DENIED);
  EXPECT_EQ(result.status().message(), "no entry");
}

TEST(StatusOr, StatusOrStatusOrIntValue) {
  StatusOr<StatusOr<int>> s{3};
  EXPECT_EQ(s.ValueOrDie().ValueOrDie(), 3);
}

TEST(StatusOr, StatusOrStatusOrIntError) {
  StatusOr<StatusOr<int>> s{StatusOr<int>{Status(ZX_ERR_ACCESS_DENIED)}};
  EXPECT_EQ(s.ValueOrDie().status().code(), ZX_ERR_ACCESS_DENIED);
}

TEST(StatusOr, UniquePtr) {
  auto ptr = std::make_unique<int>(3);

  StatusOr<std::unique_ptr<int>> value(std::move(ptr));
  EXPECT_EQ(*value.ValueOrDie(), 3);

  auto ptr2 = value.ConsumeValueOrDie();
  EXPECT_EQ(*ptr2, 3);
}

TEST(StatusOr, BadAccess) {
  auto bad_access = []() {
    StatusOr<int> s = Status(ZX_ERR_BAD_PATH);
    (void)s.ValueOrDie();
  };
  ASSERT_DEATH(bad_access, "Expected ValueOrDie to kill process.");
}

TEST(StatusOr, BadConsume) {
  auto bad_access = []() {
    StatusOr<int> s = Status(ZX_ERR_BAD_PATH);
    (void)s.ConsumeValueOrDie();
  };
  ASSERT_DEATH(bad_access, "Expected ValueOrDie to kill process.");
}

}  // namespace
}  // namespace audio::intel_hda
