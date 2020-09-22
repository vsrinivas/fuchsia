// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <lib/affine/ratio.h>
#include <lib/affine/transform.h>
#include <lib/fit/function.h>
#include <zxtest/zxtest.h>

namespace {
enum class Fatal { No, Yes };
}  // namespace

TEST(TransformTestCase, Construction) {
  // Default constructor should produce the identitiy transformation
  {
    affine::Transform transform;
    ASSERT_EQ(transform.a_offset(), 0);
    ASSERT_EQ(transform.b_offset(), 0);
    ASSERT_EQ(transform.numerator(), 1);
    ASSERT_EQ(transform.denominator(), 1);
  }

  struct TestVector {
    int64_t a_offset, b_offset;
    uint32_t N, D;
    Fatal expect_fatal;
  };

  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{  12345,  98764,       3,       2, Fatal::No  },
        TestVector{ -12345,  98764,     247,     931, Fatal::No  },
        TestVector{ -12345, -98764,   48000,   44100, Fatal::No  },
        TestVector{  12345, -98764, 1000007, 1000000, Fatal::No  },
        TestVector{  12345,  98764,       0, 1000000, Fatal::No  },
        TestVector{  12345,  98764, 1000007,       0, Fatal::Yes },
    };
  // clang-format on

  for (const auto& V : TEST_VECTORS) {
    // Check the linear form (no offsets)
    if (V.expect_fatal == Fatal::No) {
      affine::Ratio ratio{V.N, V.D};
      affine::Transform transform{ratio};

      ASSERT_EQ(transform.a_offset(), 0);
      ASSERT_EQ(transform.b_offset(), 0);
      ASSERT_EQ(transform.numerator(), ratio.numerator());
      ASSERT_EQ(transform.denominator(), ratio.denominator());
    } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
      ASSERT_DEATH(([&V]() { affine::Transform transform{affine::Ratio{V.N, V.D}}; }));
    }

    // Check the affine form (yes offsets)
    if (V.expect_fatal == Fatal::No) {
      affine::Ratio ratio{V.N, V.D};
      affine::Transform transform{V.a_offset, V.b_offset, ratio};

      ASSERT_EQ(transform.a_offset(), V.a_offset);
      ASSERT_EQ(transform.b_offset(), V.b_offset);
      ASSERT_EQ(transform.numerator(), ratio.numerator());
      ASSERT_EQ(transform.denominator(), ratio.denominator());
    } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
      ASSERT_DEATH(([&V]() {
        affine::Transform transform{V.a_offset, V.b_offset, affine::Ratio{V.N, V.D}};
      }));
    }
  }
}

TEST(TransformTestCase, Inverse) {
  struct TestVector {
    int64_t a_offset, b_offset;
    uint32_t N, D;
  };

  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{  12345,  98764,       3,       2 },
        TestVector{ -12345,  98764,     247,     931 },
        TestVector{ -12345, -98764,   48000,   44100 },
        TestVector{  12345, -98764, 1000007, 1000000 },
        TestVector{  12345,  98764,       0, 1000000 },
    };
  // clang-format on

  for (const auto& V : TEST_VECTORS) {
    affine::Ratio ratio{V.N, V.D};
    affine::Transform transform{V.a_offset, V.b_offset, ratio};

    if (transform.invertible()) {
      affine::Transform res = transform.Inverse();
      ASSERT_EQ(transform.a_offset(), res.b_offset());
      ASSERT_EQ(transform.b_offset(), res.a_offset());
      ASSERT_EQ(transform.numerator(), res.denominator());
      ASSERT_EQ(transform.denominator(), res.numerator());
      ASSERT_TRUE(transform.ratio().Inverse().numerator() == res.ratio().numerator());
      ASSERT_TRUE(transform.ratio().Inverse().denominator() == res.ratio().denominator());
    } else if constexpr (ZX_DEBUG_ASSERT_IMPLEMENTED) {
      ASSERT_DEATH(([&transform]() { transform.Inverse(); }));
    }
  }
}

TEST(TransformTestCase, Apply) {
  using Transform = affine::Transform;
  using Saturate = affine::Transform::Saturate;

  enum class Method { Static = 0, Object, Operator };
  enum class Ovfl { No = 0, Yes };

  struct TestVector {
    int64_t a_offset, b_offset;
    uint32_t N, D;
    int64_t val;
    int64_t expected;
    Ovfl expect_ovfl;
  };

  constexpr int64_t kMinI64 = std::numeric_limits<int64_t>::min();
  constexpr int64_t kMaxI64 = std::numeric_limits<int64_t>::max();
  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{  0,   0,     1,     1, 12345, 12345, Ovfl::No },
        TestVector{ 50,   0,     1,     1, 12345, 12295, Ovfl::No },
        TestVector{  0, -50,     1,     1, 12345, 12295, Ovfl::No },
        TestVector{ 50, -50,     1,     1, 12345, 12245, Ovfl::No },
        TestVector{ 50,  50,     1,     1, 12345, 12345, Ovfl::No },

        TestVector{  0,   0, 48000, 44100, 12345, 13436, Ovfl::No },
        TestVector{ 50,   0, 48000, 44100, 12345, 13382, Ovfl::No },
        TestVector{  0, -54, 48000, 44100, 12345, 13382, Ovfl::No },
        TestVector{ 50, -54, 48000, 44100, 12345, 13328, Ovfl::No },
        TestVector{ 50,  54, 48000, 44100, 12345, 13436, Ovfl::No },

        // Overflow/underflow during the A_offset stage.
        TestVector{ -100, -17, 1, 1, kMaxI64 - 1, kMaxI64 - 17, Ovfl::Yes },
        TestVector{  100,  17, 1, 1, kMinI64 + 1, kMinI64 + 17, Ovfl::Yes },

        // Overflow/underflow during the Scaling stage.
        TestVector{ 0, -17, 3, 1, kMaxI64 / 2, kMaxI64 - 17, Ovfl::Yes },
        TestVector{ 0,  17, 3, 1, kMinI64 / 2, kMinI64 + 17, Ovfl::Yes },

        // Overflow/underflow during the B_offset stage.
        TestVector{ 0,  17, 1, 1, kMaxI64 - 10, kMaxI64, Ovfl::Yes },
        TestVector{ 0, -17, 1, 1, kMinI64 + 10, kMinI64, Ovfl::Yes },
    };
  // clang-format on

  constexpr std::array METHODS{
      Method::Static,
      Method::Object,
      Method::Operator,
  };

  for (const auto& V : TEST_VECTORS) {
    for (auto method : METHODS) {
      // Test the forward transformation
      Transform T{V.a_offset, V.b_offset, {V.N, V.D}};
      int64_t res_sat, res_nosat;

      switch (method) {
        case Method::Static:
          res_sat = Transform::Apply(T.a_offset(), T.b_offset(), T.ratio(), V.val);
          res_nosat = Transform::Apply<Saturate::No>(T.a_offset(), T.b_offset(), T.ratio(), V.val);
          break;

        case Method::Object:
          res_sat = T.Apply(V.val);
          res_nosat = T.Apply<Saturate::No>(V.val);
          break;

        case Method::Operator:
          res_sat = T(V.val);
          res_nosat = T.operator()<Saturate::No>(V.val);
          break;
      }

      auto CheckExpected = [&](int64_t actual, const TestVector& V, const Transform& T,
                               Method method) {
        ASSERT_EQ(actual, V.expected,
                  "((%ld - %ld) * (%u/%u)) + %ld should be %ld; "
                  "got %ld instead (method %u)\n",
                  V.val, T.a_offset(), T.numerator(), T.denominator(), T.b_offset(), V.expected,
                  actual, static_cast<uint32_t>(method));
      };

      // Make sure the saturated result matches our expectations.
      CheckExpected(res_sat, V, T, method);

      // If we don't expect this test vector to overflow, then check to
      // make sure that the non-saturated result matches the saturated
      // result.
      if (V.expect_ovfl == Ovfl::No) {
        CheckExpected(res_nosat, V, T, method);
      }

      // Test inverse transformations operations, but only if the
      // transformation is invertible.  Otherwise test for death.
      fit::function<void()> func_sat;
      fit::function<void()> func_nosat;
      switch (method) {
        case Method::Static:
          func_sat = [&T, &V, &res_sat]() {
            if (T.invertible()) {
              auto T_inv = T.Inverse();
              res_sat =
                  Transform::ApplyInverse(T_inv.a_offset(), T_inv.b_offset(), T_inv.ratio(), V.val);
            } else {
              Transform::ApplyInverse(T.a_offset(), T.b_offset(), T.ratio(), V.val);
            }
          };

          func_nosat = [&T, &V, &res_nosat]() {
            if (T.invertible()) {
              auto T_inv = T.Inverse();
              res_nosat = Transform::ApplyInverse<Saturate::No>(T_inv.a_offset(), T_inv.b_offset(),
                                                                T_inv.ratio(), V.val);
            } else {
              Transform::ApplyInverse<Saturate::No>(T.a_offset(), T.b_offset(), T.ratio(), V.val);
            }
          };
          break;

        case Method::Object:
          func_sat = [&T, &V, &res_sat]() {
            if (T.invertible()) {
              auto T_inv = T.Inverse();
              res_sat = T_inv.ApplyInverse(V.val);
            } else {
              T.ApplyInverse(V.val);
            }
          };

          func_nosat = [&T, &V, &res_nosat]() {
            if (T.invertible()) {
              auto T_inv = T.Inverse();
              res_nosat = T_inv.ApplyInverse<Saturate::No>(V.val);
            } else {
              T.ApplyInverse<Saturate::No>(V.val);
            }
          };
          break;

        // Note: that the functor operator method has no inverse, so we skip
        // the test in that case.
        case Method::Operator:
          continue;
      }

      if (T.invertible()) {
        func_sat();
        CheckExpected(res_sat, V, T, method);

        if (V.expect_ovfl == Ovfl::No) {
          CheckExpected(res_nosat, V, T, method);
        }
      } else {
        auto CheckDeath = [](fit::function<void()> func, const TestVector& V, const Transform& T,
                             Method method) {
          ASSERT_DEATH(std::move(func),
                       "((%ld - %ld) * (%u/%u)) + %ld should have resulted in death "
                       "(method %u)\n",
                       V.val, T.a_offset(), T.numerator(), T.denominator(), T.b_offset(),
                       static_cast<uint32_t>(method));
        };
        CheckDeath(std::move(func_sat), V, T, method);
        CheckDeath(std::move(func_nosat), V, T, method);
      }
    }
  }
}

TEST(TransformTestCase, Compose) {
  using Transform = affine::Transform;

  enum class Method { Static = 0, Operator };
  enum class Exact { No = 0, Yes };

  constexpr int64_t kMinI64 = std::numeric_limits<int64_t>::min();
  constexpr int64_t kMaxI64 = std::numeric_limits<int64_t>::max();

  struct TestVector {
    Transform ab;
    Transform bc;
    Transform ac;
    Exact is_exact;
  };

  // TODO(johngro) : If we ever make the Ratio/Transform constructors
  // constexpr, then come back and make this constexpr.  Right now, they are
  // not because of the assert-checking behavior in the Ratio constructor.
  // clang-format off
    const std::array TEST_VECTORS {
        // Identity(Identity(a)) == Identity(a)
        TestVector{
            { 0, 0, { 1, 1 } },
            { 0, 0, { 1, 1 } },
            { 0, 0, { 1, 1 } },
            Exact::Yes
        },

        // F(Identity(a)) == F(a)
        //
        // TODO(fxbug.dev/13293): Note that this does not currently produce the exact
        // same result, or even an equivalent result.  The intermediate offset
        // of the composition of bc(ab(a)) is -12345, and the current
        // composition implementation always attempts to move this to the
        // b_offset side of the composed function.  In this case, that means
        // running the -12345 through the 17/7 ratio, which results in some
        // offset rounding error.  For now, however, this is the expected
        // behavior of the current implementation.  If/when MTWN-6 is resolved,
        // this test vector will start to fail and will need to be updated.
        TestVector{
            {     0,     0, {  1, 1 } },
            { 12345, 98765, { 17, 7 } },
            {     0, 68784, { 17, 7 } },
            Exact::Yes
        },

        // Identity(F(a)) == F(a)
        TestVector{
            { 12345, 98765, { 17, 7 } },
            {     0,     0, {  1, 1 } },
            { 12345, 98765, { 17, 7 } },
            Exact::Yes
        },

        // A moderately complicated example, but still an exact one.
        // BC(AB(a)) == AC(a)
        TestVector{
            { 34327,   86539, { 1000007, 1000000 } },
            { 728376, -34265, {   48000,   44100 } },
            { 34327, -732864, { 1000007,  918750 } },
            Exact::Yes
        },

        // Overflow saturation of the intermediate offset before distribution.
        TestVector{
            {    0, kMaxI64 - 5, { 1, 1 } },
            { -100,           0, { 1, 1 } },
            {    0,     kMaxI64, { 1, 1 } },
            Exact::Yes
        },

        // Underflow saturation of the intermediate offset before distribution.
        TestVector{
            {   0, kMinI64 + 5, { 1, 1 } },
            { 100,           0, { 1, 1 } },
            {   0,     kMinI64, { 1, 1 } },
            Exact::Yes
        },

        // Overflow saturation AC.b_offset after distribution.
        TestVector{
            {    0,         100, { 1, 1 } },
            {    0, kMaxI64 - 5, { 1, 1 } },
            {    0,     kMaxI64, { 1, 1 } },
            Exact::Yes
        },

        // Underflow saturation AC.b_offset after distribution.
        TestVector{
            {    0,        -100, { 1, 1 } },
            {    0, kMinI64 + 5, { 1, 1 } },
            {    0,     kMinI64, { 1, 1 } },
            Exact::Yes
        },

        // TODO(fxbug.dev/13293): Right now, it is impossible to under/overflow saturate
        // the AC.a_offset side of the composed function, because the current
        // implementation always distributes the intermediate offset entirely to
        // the C side of the equation.  When this changes, we need to add test
        // vectors to make sure that these cases behave properly.

        // Composition of the ratio which requires a loss of precision.  Note
        // that these fractions were taken from the Ratio tests.  Each numerator
        // and denominator is made up of 3 prime numbers, none of them in
        // common.
        TestVector{
            { 0,  0, { 3465653567, 2327655023 } },
            { 0,  0, { 1291540343, 3698423317 } },
            { 0,  0, {  317609835,  610852072 } },
            Exact::No
        },

        // Same idea, but this time, include an intermediate offset.  The offset
        // should be distributed before the ratios are combined, resulting in no
        // loss of precision (in this specific case) of the intermediate
        // distribution.
        TestVector{
            {                0,             20, { 3465653567, 2327655023 } },
            { -3698423317 + 20,              5, { 1291540343, 3698423317 } },
            {                0, 1291540343 + 5, {  317609835,  610852072 } },
            Exact::No
        },
    };
  // clang-format on

  constexpr std::array METHODS{
      Method::Static,
      Method::Operator,
  };

  for (const auto& V : TEST_VECTORS) {
    for (auto method : METHODS) {
      fit::function<void()> func;
      Transform result;

      switch (method) {
        case Method::Static:
          func = [&result, &V]() { result = Transform::Compose(V.bc, V.ab); };
          break;
        case Method::Operator:
          func = [&result, &V]() { result = V.bc * V.ab; };
          break;
      }

      auto VerifyResult = [](const TestVector& V, const Transform& result, Method method) {
        bool match = (result.a_offset() == V.ac.a_offset()) &&
                     (result.b_offset() == V.ac.b_offset()) &&
                     (result.numerator() == V.ac.numerator()) &&
                     (result.denominator() == V.ac.denominator());
        ASSERT_TRUE(match,
                    "[ %ld : %u/%u : %ld ] <--> [ %ld : %u/%u : %ld ] should produce "
                    "[ %ld : %u/%u : %ld ] ; got [ %ld : %u/%u : %ld ] instead (method %u)",
                    V.ab.a_offset(), V.ab.numerator(), V.ab.denominator(), V.ab.b_offset(),
                    V.bc.a_offset(), V.bc.numerator(), V.bc.denominator(), V.bc.b_offset(),
                    V.ac.a_offset(), V.ac.numerator(), V.ac.denominator(), V.ac.b_offset(),
                    result.a_offset(), result.numerator(), result.denominator(), result.b_offset(),
                    static_cast<uint32_t>(method));
      };

      // If the composition is expected to produce an exact result, then
      // compute and validate the result.  Otherwise, assert that the
      // composition operation produces death as expected.
      if (V.is_exact == Exact::Yes) {
        func();
        VerifyResult(V, result, method);
      } else {
        ASSERT_DEATH(std::move(func),
                     "Expected death during composition : "
                     "[ %ld : %u/%u : %ld ] <--> [ %ld : %u/%u : %ld ] (method %u)",
                     V.ab.a_offset(), V.ab.numerator(), V.ab.denominator(), V.ab.b_offset(),
                     V.bc.a_offset(), V.bc.numerator(), V.bc.denominator(), V.bc.b_offset(),
                     static_cast<uint32_t>(method));
      }

      // If this is not the operator form of composition, test the inexact
      // version of composition.  The expected result in the test vector
      // should match the inexact result.
      if (method == Method::Static) {
        result = Transform::Compose(V.bc, V.ab, Transform::Exact::No);
        VerifyResult(V, result, method);
      }
    }
  }
}
