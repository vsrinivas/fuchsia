// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "src/camera/lib/numerics/rational.h"

namespace camera {
namespace {

using numerics::Rational;

TEST(Numerics, RationalTrivial) {
  for (int64_t a : {-1, 0, 1}) {
    for (int64_t b : {-1, 0, 1}) {
      Rational r1{.n = a};
      Rational r2{.n = b};
      Rational expect_add{.n = a + b};
      EXPECT_EQ(r1 + r2, expect_add);
      Rational expect_sub{.n = a - b};
      EXPECT_EQ(r1 - r2, expect_sub);
      Rational expect_mul{.n = a * b};
      EXPECT_EQ(r1 * r2, expect_mul);
      if (b != 0) {
        Rational expect_div{.n = a / b};
        EXPECT_EQ(r1 / r2, expect_div);
      }
    }
  }
}

TEST(Numerics, RationalComplex) {
  Rational r1{.n = 22, .d = 7};
  Rational r2{.n = 355, .d = 113};
  EXPECT_EQ(r1 + r2, (Rational{.n = 4971, .d = 791}));
  EXPECT_EQ(r1 - r2, (Rational{.n = 1, .d = 791}));
  EXPECT_EQ(r1 * r2, (Rational{.n = 7810, .d = 791}));
  EXPECT_EQ(r1 / r2, (Rational{.n = 2486, .d = 2485}));
  EXPECT_EQ(r2 + r1, (Rational{.n = 4971, .d = 791}));
  EXPECT_EQ(r2 - r1, (Rational{.n = -1, .d = 791}));
  EXPECT_EQ(r2 * r1, (Rational{.n = 7810, .d = 791}));
  EXPECT_EQ(r2 / r1, (Rational{.n = 2485, .d = 2486}));
  EXPECT_EQ(+r1, (Rational{.n = 22, .d = 7}));
  EXPECT_EQ(-r1, (Rational{.n = -22, .d = 7}));
  auto x1 = r1;
  x1 += r2;
  EXPECT_EQ(x1, r1 + r2);
  auto x2 = r1;
  x2 -= r2;
  EXPECT_EQ(x2, r1 - r2);
  auto x3 = r1;
  x3 *= r2;
  EXPECT_EQ(x3, r1 * r2);
  auto x4 = r1;
  x4 /= r2;
  EXPECT_EQ(x4, r1 / r2);
}

TEST(Numerics, RationalComparators) {
  Rational a{.n = 22, .d = 7};
  Rational b{.n = 34, .d = 21};
  Rational c{.n = 3, .d = 5};

  EXPECT_FALSE(a == b);
  EXPECT_FALSE(b == a);
  EXPECT_TRUE(c == c);

  EXPECT_TRUE(a != b);
  EXPECT_TRUE(b != a);
  EXPECT_FALSE(c != c);

  EXPECT_FALSE(a < b);
  EXPECT_TRUE(b < a);
  EXPECT_FALSE(c < c);

  EXPECT_FALSE(a <= b);
  EXPECT_TRUE(b <= a);
  EXPECT_TRUE(c <= c);

  EXPECT_TRUE(a > b);
  EXPECT_FALSE(b > a);
  EXPECT_FALSE(c > c);

  EXPECT_TRUE(a >= b);
  EXPECT_FALSE(b >= a);
  EXPECT_TRUE(c >= c);
}

TEST(Numerics, Reduction) {
  Rational ref{.n = 22, .d = 7};

  Rational a{.n = -22022, .d = -7007};
  EXPECT_EQ(Reduce(a), ref);

  Rational b{.n = -22, .d = -7};
  EXPECT_EQ(Reduce(b), ref);

  Rational c{.n = -22, .d = 7};
  EXPECT_EQ(Reduce(c), -ref);

  Rational d{.n = 22, .d = -7};
  EXPECT_EQ(Reduce(d), -ref);

  Rational e{.n = 22, .d = 7};
  EXPECT_EQ(Reduce(e), ref);
}

}  // namespace
}  // namespace camera
