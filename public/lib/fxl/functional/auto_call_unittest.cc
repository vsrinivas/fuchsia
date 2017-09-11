// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/functional/auto_call.h"

#include <memory>

#include "gtest/gtest.h"

namespace fxl {
namespace {

void reset_arg(int* p) {
  *p = 0;
}

void helper_basic (int* var_ptr) {
  auto reset = fxl::MakeAutoCall([var_ptr](){ reset_arg(var_ptr); });
}

TEST(AutoCallTest, Basic) {
  int var = 42;
  helper_basic(&var);
  EXPECT_EQ(var, 0);
}

void helper_cancel (int* var_ptr) {
  auto reset = fxl::MakeAutoCall([var_ptr](){ reset_arg(var_ptr); });
  reset.cancel();
}

TEST(AutoCallTest, Cancel) {
  int var = 42;
  helper_cancel(&var);
  EXPECT_EQ(var, 42);
}

void helper_call (int* var_ptr) {
  auto reset = fxl::MakeAutoCall([var_ptr](){ reset_arg(var_ptr); });
  reset.call();
}

TEST(AutoCallTest, Call) {
  int var = 42;
  helper_call(&var);
  EXPECT_EQ(var, 0);
}

}  // namespace
}  // namespace fxl
