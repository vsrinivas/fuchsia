// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/err_or.h"

#include "gtest/gtest.h"

namespace zxdb {

// This mostly tests that things compile. There is also a set of errors that assert which might
// be nice to death test but we don't do those.

TEST(ErrOr, Basic) {
  ErrOr<int> err_or_int = 5;
  EXPECT_FALSE(err_or_int.has_error());
  EXPECT_TRUE(err_or_int.ok());
  EXPECT_TRUE(err_or_int);

  EXPECT_EQ(5, err_or_int.value());
  EXPECT_EQ(5, err_or_int.value());  // Above getter should not have mutated.
  EXPECT_EQ(5, err_or_int.value_or_empty());
  EXPECT_EQ(Err(), err_or_int.err_or_empty());

  // Error case.
  err_or_int = Err("bad");
  EXPECT_TRUE(err_or_int.has_error());
  EXPECT_FALSE(err_or_int.ok());
  EXPECT_FALSE(err_or_int);

  EXPECT_EQ("bad", err_or_int.err().msg());
  EXPECT_EQ(0, err_or_int.value_or_empty());

  // Owning a more complex object.
  ErrOr<std::unique_ptr<int>> err_or_ptr = std::make_unique<int>(6);
  EXPECT_FALSE(err_or_ptr.has_error());
  EXPECT_TRUE(err_or_ptr.ok());

  // Test moving out.
  auto ptr = err_or_ptr.take_value_or_empty();
  EXPECT_EQ(6, *ptr);
  EXPECT_FALSE(err_or_ptr.value().get());
}

}  // namespace zxdb
