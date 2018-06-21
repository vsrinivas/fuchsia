// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/functional/auto_call.h"

#include "lib/fxl/functional/closure.h"

#include <memory>

#include "gtest/gtest.h"

namespace fxl {
namespace {

void incr_arg(int* p) { *p += 1; }

TEST(AutoCallTest, NullConstructor) {
  {
    fxl::AutoCall<fxl::Closure> c(nullptr);
    // No crash is a successful test.
  }
}

TEST(AutoCallTest, Basic) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 1);
}

TEST(AutoCallTest, Cancel) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    EXPECT_EQ(var, 0);
    do_incr.cancel();
    EXPECT_EQ(var, 0);
    // Once cancelled, call has no effect.
    do_incr.call();
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 0);
}

TEST(AutoCallTest, Call) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    EXPECT_EQ(var, 0);
    do_incr.call();
    EXPECT_EQ(var, 1);
    // Call is effective only once.
    do_incr.call();
    EXPECT_EQ(var, 1);
  }
  EXPECT_EQ(var, 1);
}

TEST(AutoCallTest, RecursiveCall) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall<fxl::Closure>([]() { /* no-op */ });
    do_incr = fxl::MakeAutoCall<fxl::Closure>([&do_incr, &var]() {
      incr_arg(&var);
      do_incr.call();
    });
    EXPECT_EQ(var, 0);
    do_incr.call();
    EXPECT_EQ(var, 1);
  }
  EXPECT_EQ(var, 1);
}

TEST(AutoCallTest, MoveConstructBasic) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    auto do_incr2(std::move(do_incr));
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 1);
}

TEST(AutoCallTest, MoveConstructFromCancelled) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    do_incr.cancel();
    auto do_incr2(std::move(do_incr));
    EXPECT_EQ(var, 0);
  }
  EXPECT_EQ(var, 0);
}

TEST(AutoCallTest, MoveConstructFromCalled) {
  int var = 0;
  {
    auto do_incr = fxl::MakeAutoCall([&var]() { incr_arg(&var); });
    EXPECT_EQ(var, 0);
    do_incr.call();
    EXPECT_EQ(var, 1);
    // Must not be called again, since do_incr has triggered already.
    auto do_incr2(std::move(do_incr));
  }
  EXPECT_EQ(var, 1);
}

TEST(AutoCallTest, MoveAssignBasic) {
  int var1 = 0, var2 = 0;
  {
    auto do_incr =
        fxl::MakeAutoCall<fxl::Closure>([&var1]() { incr_arg(&var1); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    auto do_incr2 =
        fxl::MakeAutoCall<fxl::Closure>([&var2]() { incr_arg(&var2); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);
  }
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

TEST(AutoCallTest, MoveAssignWiderScope) {
  int var1 = 0, var2 = 0;
  {
    auto do_incr =
        fxl::MakeAutoCall<fxl::Closure>([&var1]() { incr_arg(&var1); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    {
      auto do_incr2 =
          fxl::MakeAutoCall<fxl::Closure>([&var2]() { incr_arg(&var2); });
      EXPECT_EQ(var1, 0);
      EXPECT_EQ(var2, 0);
      // do_incr is moved-to, so its associated function is called.
      do_incr = std::move(do_incr2);
      EXPECT_EQ(var1, 1);
      EXPECT_EQ(var2, 0);
    }
    // do_incr2 is out of scope but has been moved so its function is not
    // called.
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 0);
  }
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

TEST(AutoCallTest, MoveAssignFromCancelled) {
  int var1 = 0, var2 = 0;
  {
    auto do_incr =
        fxl::MakeAutoCall<fxl::Closure>([&var1]() { incr_arg(&var1); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    auto do_incr2 =
        fxl::MakeAutoCall<fxl::Closure>([&var2]() { incr_arg(&var2); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    do_incr.cancel();
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 1);
  }
  // do_incr was cancelled, this state is preserved by the move.
  EXPECT_EQ(var1, 0);
  EXPECT_EQ(var2, 1);
}

TEST(AutoCallTest, MoveAssignFromCalled) {
  int var1 = 0, var2 = 0;
  {
    auto do_incr =
        fxl::MakeAutoCall<fxl::Closure>([&var1]() { incr_arg(&var1); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    auto do_incr2 =
        fxl::MakeAutoCall<fxl::Closure>([&var2]() { incr_arg(&var2); });
    EXPECT_EQ(var1, 0);
    EXPECT_EQ(var2, 0);
    do_incr.call();
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 0);
    // do_incr2 is moved-to, so its associated function is called.
    do_incr2 = std::move(do_incr);
    EXPECT_EQ(var1, 1);
    EXPECT_EQ(var2, 1);
  }
  // do_incr was called already, this state is preserved by the move.
  EXPECT_EQ(var1, 1);
  EXPECT_EQ(var2, 1);
}

}  // namespace
}  // namespace fxl
