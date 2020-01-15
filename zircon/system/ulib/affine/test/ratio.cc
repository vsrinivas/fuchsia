// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <lib/affine/ratio.h>
#include <lib/fit/function.h>
#include <type_traits>
#include <zxtest/zxtest.h>

namespace {

enum class Fatal { No, Yes };

template <typename T>
struct ReductionTestVector {
  T initial_n, initial_d;
  T expected_n, expected_d;
  Fatal expect_fatal;
};

template <typename T, size_t VECTOR_COUNT>
void ReductionHelper(const std::array<ReductionTestVector<T>, VECTOR_COUNT>& vectors) {
  const char* tag;
  if (std::is_same_v<T, uint32_t>) {
    tag = "uint32_t";
  } else if (std::is_same_v<T, uint64_t>) {
    tag = "uint64_t";
  } else {
    tag = "<unknown>";
  }

  // Test reduction using the static method
  for (const auto& V : vectors) {
    T N = V.initial_n;
    T D = V.initial_d;

    if (V.expect_fatal == Fatal::No) {
      affine::Ratio::Reduce<T>(&N, &D);

      ASSERT_TRUE((N == V.expected_n) && (D == V.expected_d),
                  "Expected %s %lu/%lu to reduce to %lu/%lu; got %lu/%lu instead.", tag,
                  static_cast<uint64_t>(V.initial_n), static_cast<uint64_t>(V.initial_d),
                  static_cast<uint64_t>(V.expected_n), static_cast<uint64_t>(V.expected_d),
                  static_cast<uint64_t>(N), static_cast<uint64_t>(D));
    } else {
      ASSERT_DEATH([&V]() {
        T N = V.initial_n;
        T D = V.initial_d;
        affine::Ratio::Reduce<T>(&N, &D);
      });
    }

    // If we are testing 32-bit vectors, also test in-place reduction.
    if constexpr (std::is_same_v<T, uint32_t>) {
      if (V.expect_fatal == Fatal::No) {
        affine::Ratio R{V.initial_n, V.initial_d};
        R.Reduce();

        ASSERT_TRUE((R.numerator() == V.expected_n) && (R.denominator() == V.expected_d),
                    "Expected %s %lu/%lu to reduce to %lu/%lu; got %lu/%lu instead.", tag,
                    static_cast<uint64_t>(V.initial_n), static_cast<uint64_t>(V.initial_d),
                    static_cast<uint64_t>(V.expected_n), static_cast<uint64_t>(V.expected_d),
                    static_cast<uint64_t>(R.numerator()), static_cast<uint64_t>(R.denominator()));
      } else {
        ASSERT_DEATH([&V]() {
          affine::Ratio R(V.initial_n, V.initial_d);
          R.Reduce();
        });
      }
    }
  }
}
}  // namespace

TEST(RatioTestCase, Construction) {
  struct TestVector {
    uint32_t N, D;
    Fatal expect_fatal;
  };

  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{ 0,   1, Fatal::No },
        TestVector{ 1,   1, Fatal::No },
        TestVector{ 23, 41, Fatal::No },
        TestVector{ 1,   0, Fatal::Yes },
    };
  // clang-format on

  // Test that explicit construction and the numerator/denominator accessors
  // are working properly.
  for (const auto& V : TEST_VECTORS) {
    if (V.expect_fatal == Fatal::No) {
      affine::Ratio R{V.N, V.D};
      ASSERT_EQ(R.numerator(), V.N);
      ASSERT_EQ(R.denominator(), V.D);
    } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
      ASSERT_DEATH(([&V]() { affine::Ratio R{V.N, V.D}; }));
    }
  }

  // Test that the default constructor produces 1/1
  {
    affine::Ratio R;
    ASSERT_EQ(R.numerator(), 1);
    ASSERT_EQ(R.denominator(), 1);
  }

  // Test that reduction is _not_ automatically performed.
  {
    affine::Ratio R{9, 21};
    ASSERT_EQ(R.numerator(), 9);
    ASSERT_EQ(R.denominator(), 21);
  }
}

TEST(RatioTestCase, Reduction) {
  // clang-format off
    constexpr std::array Vectors32 {
        ReductionTestVector<uint32_t>{       1,       1,       1,       1, Fatal::No },
        ReductionTestVector<uint32_t>{      10,      10,       1,       1, Fatal::No },
        ReductionTestVector<uint32_t>{      10,       2,       5,       1, Fatal::No },
        ReductionTestVector<uint32_t>{       0,       1,       0,       1, Fatal::No },
        ReductionTestVector<uint32_t>{       0,     500,       0,       1, Fatal::No },
        ReductionTestVector<uint32_t>{   48000,   44100,     160,     147, Fatal::No },
        ReductionTestVector<uint32_t>{   44100,   48000,     147,     160, Fatal::No },
        ReductionTestVector<uint32_t>{ 1000007, 1000000, 1000007, 1000000, Fatal::No },
        ReductionTestVector<uint32_t>{       0,       0,       0,       0, Fatal::Yes },
        ReductionTestVector<uint32_t>{       1,       0,       0,       0, Fatal::Yes },
        ReductionTestVector<uint32_t>{ std::numeric_limits<uint32_t>::max(), 0, 0, 0, Fatal::Yes },
    };

    constexpr std::array Vectors64 {
        ReductionTestVector<uint64_t>{           1,           1,       1,       1, Fatal::No },
        ReductionTestVector<uint64_t>{          10,          10,       1,       1, Fatal::No },
        ReductionTestVector<uint64_t>{          10,           2,       5,       1, Fatal::No },
        ReductionTestVector<uint64_t>{           0,           1,       0,       1, Fatal::No },
        ReductionTestVector<uint64_t>{           0,         500,       0,       1, Fatal::No },
        ReductionTestVector<uint64_t>{       48000,       44100,     160,     147, Fatal::No },
        ReductionTestVector<uint64_t>{       44100,       48000,     147,     160, Fatal::No },
        ReductionTestVector<uint64_t>{     1000007,     1000000, 1000007, 1000000, Fatal::No },
        ReductionTestVector<uint64_t>{ 48000336000, 44100000000, 1000007,  918750, Fatal::No },
        ReductionTestVector<uint64_t>{           0,           0,       0,       0, Fatal::Yes },
        ReductionTestVector<uint64_t>{           1,           0,       0,       0, Fatal::Yes },
        ReductionTestVector<uint64_t>{ std::numeric_limits<uint64_t>::max(), 0, 0, 0, Fatal::Yes },
    };
  // clang-format on

  ASSERT_NO_FAILURES(ReductionHelper(Vectors32));
  ASSERT_NO_FAILURES(ReductionHelper(Vectors64));
}

TEST(RatioTestCase, Product) {
  using Exact = affine::Ratio::Exact;
  struct TestVector {
    uint32_t a_n, a_d;
    uint32_t b_n, b_d;
    uint32_t expected_n, expected_d;
    Exact exact;
    Fatal expect_fatal;
  };

  // clang-format off
    constexpr std::array TEST_VECTORS {
        // Straight-forward cases with exact solutions.
        TestVector{    1,     1,       1,       1,       1,      1, Exact::Yes, Fatal::No },
        TestVector{    0,     1,       1,       1,       0,      1, Exact::Yes, Fatal::No },
        TestVector{    0,   500,       1,       1,       0,      1, Exact::Yes, Fatal::No },
        TestVector{    3,     4,       5,       9,       5,     12, Exact::Yes, Fatal::No },
        TestVector{48000, 44100, 1000007, 1000000, 1000007, 918750, Exact::Yes, Fatal::No },

        // cases with a zero denominator.  These should be fatal
        TestVector{    0,     0,       0,       0,       0,     0,  Exact::Yes, Fatal::Yes },
        TestVector{   10,     0,     200,     300,       0,     0,  Exact::Yes, Fatal::Yes },
        TestVector{   10,    20,     200,       0,       0,     0,  Exact::Yes, Fatal::Yes },

        // Test a case which lacks a precise solution.  We should either get a
        // degraded form, or panic, depending on whether we demand an exact
        // solution.
        //
        // Note that this is a particularly brutal test case.  Both of the
        // fractions involved are pushing the limits of 32 bit storage, and none
        // of the numerators nor denominators share _any_ prime factors.
        //
        // Finally, the test for the approximate solution given here is
        // algorithm specific.  If the algorithm is changed, either to increase
        // accuracy, or to increase performance, this test vector will need to be
        // updated.
        //
        //  739 * 829 * 5657      2999 * 127 * 3391     3465653567     1291540343
        // ------------------- * ------------------- = ------------ * ------------
        //  997 * 1609 * 1451     149 * 6173 * 4021     2327655023     3698423317
        //
        //                                              4476031396642353481
        //                                           = ---------------------
        //                                              8608653610995371291
        //
        TestVector{ 3465653567, 2327655023, 1291540343, 3698423317, 0, 0, Exact::Yes, Fatal::Yes },
        TestVector{ 3465653567, 2327655023, 1291540343, 3698423317, 317609835, 610852072,
                    Exact::No, Fatal::No},

        // Test cases where the result is just a massive under or overflow.
        TestVector{ 0xFFFFFFFF, 1, 0xFFFFFFFF, 1,          0, 0, Exact::Yes, Fatal::Yes },
        TestVector{ 0xFFFFFFFF, 1, 0xFFFFFFFF, 1, 0xFFFFFFFF, 1, Exact::No,  Fatal::No  },
        TestVector{ 1, 0xFFFFFFFF, 1, 0xFFFFFFFF,          0, 0, Exact::Yes, Fatal::Yes },
        TestVector{ 1, 0xFFFFFFFF, 1, 0xFFFFFFFF,          0, 1, Exact::No,  Fatal::No  },
    };
  // clang-format on

  for (const auto& V : TEST_VECTORS) {
    // Exercise the static Product method which takes just raw integers.
    uint32_t N, D;
    if (V.expect_fatal == Fatal::No) {
      affine::Ratio::Product(V.a_n, V.a_d, V.b_n, V.b_d, &N, &D, V.exact);
      ASSERT_TRUE((N == V.expected_n) && (D == V.expected_d),
                  "Expected %u/%u * %u/%u to produce %u/%u; got %u/%u instead.", V.a_n, V.a_d,
                  V.b_n, V.b_d, V.expected_n, V.expected_d, N, D);
    } else {
      ASSERT_DEATH(([&V]() {
        uint32_t N, D;
        affine::Ratio::Product(V.a_n, V.a_d, V.b_n, V.b_d, &N, &D, V.exact);
      }));
    }

    // Exercise the static Product method which takes Ratio objects, along
    // with the * and / operator.  Verify that the operation is commutative
    // as well.  Skip any operations which involve a zero denominator.
    // These will fail during construction of the ratio object (and are
    // tested independently in the constructor tests)
    if ((V.a_d == 0) || (V.b_d == 0)) {
      continue;
    }

    affine::Ratio A{V.a_n, V.a_d};
    affine::Ratio B{V.b_n, V.b_d};

    enum class Method {
      StaticAB = 0,
      StaticBA,
      MulOperatorAB,
      MulOperatorBA,
      DivOperatorAB,
      DivOperatorBA
    };
    constexpr std::array METHODS = {Method::StaticAB,      Method::StaticBA,
                                    Method::MulOperatorAB, Method::MulOperatorBA,
                                    Method::DivOperatorAB, Method::DivOperatorBA};

    for (auto method : METHODS) {
      fit::function<void()> func;
      affine::Ratio res;

      // The operator forms demand exact results.  Skip test vectors which
      // expect non-exact results.
      if ((V.exact == Exact::No) && (method != Method::StaticAB) && (method != Method::StaticBA)) {
        continue;
      }

      // Division tests use the inversion operation to save some test
      // vector space.  Make sure to expect death instead of success if
      // this would produce division by zero.
      Fatal expect_fatal = ((method == Method::DivOperatorAB) && (B.numerator() == 0)) ||
                                   ((method == Method::DivOperatorBA) && (A.numerator() == 0))
                               ? Fatal::Yes
                               : V.expect_fatal;

      switch (method) {
        case Method::StaticAB:
          func = [&A, &B, &V, &res]() { res = affine::Ratio::Product(A, B, V.exact); };
          break;
        case Method::StaticBA:
          func = [&A, &B, &V, &res]() { res = affine::Ratio::Product(B, A, V.exact); };
          break;
        case Method::MulOperatorAB:
          func = [&A, &B, &res]() { res = A * B; };
          break;
        case Method::MulOperatorBA:
          func = [&A, &B, &res]() { res = B * A; };
          break;
        case Method::DivOperatorAB:
          func = [&A, &B, &res]() { res = A / B.Inverse(); };
          break;
        case Method::DivOperatorBA:
          func = [&A, &B, &res]() { res = B / A.Inverse(); };
          break;
      }

      if (expect_fatal == Fatal::No) {
        func();
        ASSERT_TRUE((res.numerator() == V.expected_n) && (res.denominator() == V.expected_d),
                    "Expected %u/%u * %u/%u to produce %u/%u; "
                    "got %u/%u instead (method %u).",
                    A.numerator(), A.denominator(), B.numerator(), B.denominator(), V.expected_n,
                    V.expected_d, res.numerator(), res.denominator(),
                    static_cast<uint32_t>(method));
      } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
        ASSERT_DEATH(std::move(func), "Expected Death: %u/%u * %u/%u (method %u).", A.numerator(),
                     A.denominator(), B.numerator(), B.denominator(),
                     static_cast<uint32_t>(method));
      }
    }
  }
}

TEST(RatioTestCase, Scale) {
  using Ratio = affine::Ratio;

  struct TestVector {
    int64_t val;
    uint32_t n, d;
    int64_t expected;
    Fatal expect_fatal;
  };

  enum class Method { Static, MulOperatorRatioVal, MulOperatorValRatio, DivOperator };

  // clang-format on
  constexpr std::array TEST_VECTORS{
      TestVector{0, 0, 1, 0, Fatal::No},
      TestVector{1234567890, 0, 1, 0, Fatal::No},
      TestVector{0, 1, 1, 0, Fatal::No},
      TestVector{1234567890, 1, 1, 1234567890, Fatal::No},
      TestVector{0, 1, 0, 0, Fatal::Yes},
      TestVector{1234567890, 1, 0, 0, Fatal::Yes},
      TestVector{198, 48000, 44100, 215, Fatal::No},
      TestVector{-198, 48000, 44100, -216, Fatal::No},
      TestVector{(49 * 198), 48000, 44100, 10560, Fatal::No},
      TestVector{-(49 * 198), 48000, 44100, -10560, Fatal::No},
      TestVector{0x1517ffffeae80, 0xbebc200, 0x33333333, 0x4e94914f0000, Fatal::No},
      TestVector{-0x1517ffffeae80, 0xbebc200, 0x33333333, -0x4e94914f0000, Fatal::No},

      // Overflow
      TestVector{std::numeric_limits<int64_t>::max(), 1000001, 1000000, Ratio::kOverflow,
                 Fatal::No},

      // Underflow where we spill into the upper [64, 96) bit range
      TestVector{std::numeric_limits<int64_t>::min(), 1000001, 1000000, Ratio::kUnderflow,
                 Fatal::No},

      // Underflow where bit 63 ends up set, and not all of the rest of the
      // bits are zero.
      TestVector{-0x2000000000000001, 4, 1, Ratio::kUnderflow, Fatal::No},
  };
  // clang-format off

    constexpr std::array METHODS = { Method::Static,
                                     Method::MulOperatorRatioVal,
                                     Method::MulOperatorValRatio,
                                     Method::DivOperator };

    for (const auto& V : TEST_VECTORS) {
        for (auto method : METHODS) {
            fit::function<void()> func;
            int64_t res;

            // Expect failure if we plan to divide by a ratio with a zero
            // numerator.
            Fatal expect_fatal = (method == Method::DivOperator) && (V.n == 0)
                               ? Fatal::Yes
                               : V.expect_fatal;

            switch (method) {
            case Method::Static:
                func = [&V, &res]() { res = Ratio::Scale(V.val, V.n, V.d); };
                break;
            case Method::MulOperatorRatioVal:
                func = [&V, &res]() { res = Ratio{V.n, V.d} * V.val; };
                break;
            case Method::MulOperatorValRatio:
                func = [&V, &res]() { res = V.val * Ratio{V.n, V.d}; };
                break;
            case Method::DivOperator:
                func = [&V, &res]() { res = V.val / Ratio{V.d, V.n}; };
                break;
            }

            if (expect_fatal == Fatal::No) {
                func();
                ASSERT_TRUE(res == V.expected,
                            "Expected %ld * %u/%u to produce %ld; got %ld instead (method %u).",
                            V.val, V.n, V.d, V.expected, res, static_cast<uint32_t>(method));
            } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
                ASSERT_DEATH(std::move(func),
                            "Expected Death; %ld * %u/%u (method %u).",
                            V.val, V.n, V.d, static_cast<uint32_t>(method));
            }
        }
    }
}

TEST(RatioTestCase, Inverse) {
    struct TestVector { uint32_t N, D; };
  // clang-format on
  constexpr std::array TEST_VECTORS{
      TestVector{0, 1},
      TestVector{1, 1},
      TestVector{123456, 987654},
  };
  // clang-format off

    for (const auto& V : TEST_VECTORS) {
        affine::Ratio R{ V.N, V.D };

        if (R.invertible()) {
            affine::Ratio res = R.Inverse();
            ASSERT_EQ(res.numerator(), R.denominator());
            ASSERT_EQ(res.denominator(), R.numerator());
        } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
            ASSERT_DEATH(([&R]() { R.Inverse(); }));
        }
    }
}
