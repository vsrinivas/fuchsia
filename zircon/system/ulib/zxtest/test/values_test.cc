// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>

#include <fbl/function.h>
#include <zxtest/base/assertion.h>
#include <zxtest/base/observer.h>
#include <zxtest/base/parameterized-value-impl.h>
#include <zxtest/base/runner.h>
#include <zxtest/base/test-case.h>
#include <zxtest/base/test-info.h>
#include <zxtest/base/types.h>
#include <zxtest/base/values.h>

namespace zxtest::test {

void TestValuesIn() {
  // Test std::vector
  auto c1 = std::vector<int>({0, 1, 2, 3});
  auto p1 = ::zxtest::testing::ValuesIn(c1);
  ZX_ASSERT_MSG(p1.size() == c1.size(), "Resulting provider size does not match input size.");
  size_t i = 0;
  for (auto it = c1.begin(); it != c1.end(); ++it, ++i) {
    ZX_ASSERT_MSG(p1[i] == *it, "Expected %d, got %d", p1[i], *it);
  }

  // Test std::array
  auto c2 = cpp20::to_array<int>({4, 5, 6, 7});
  auto p2 = ::zxtest::testing::ValuesIn(c2);
  ZX_ASSERT_MSG(p2.size() == c2.size(), "Resulting provider size does not match input size.");
  i = 0;
  for (auto it = c2.begin(); it != c2.end(); ++it, ++i) {
    ZX_ASSERT_MSG(p2[i] == *it, "Expected %d, got %d", p2[i], *it);
  }

  // TODO(fxb/82902): This does not work. We still don't support bools in a vector.
  // auto c3 = std::vector<bool>({false, true});
  // auto p3 = ::zxtest::testing::ValuesIn(c3);
  // ZX_ASSERT_MSG(p3.size() == c3.size(), "Resulting provider size does not match input
  // size."); i = 0; for (auto it = c3.begin(); it != c3.end(); ++it, ++i) {
  //   bool val = *it;
  //   ZX_ASSERT_MSG(p3[i] == val, "Expected %d, got %d", p3[i], val);
  // }

  auto c4 = cpp20::to_array<bool>({false, true});
  auto p4 = ::zxtest::testing::ValuesIn(c4);
  ZX_ASSERT_MSG(p4.size() == c4.size(), "Resulting provider size does not match input size.");
  i = 0;
  for (auto it = c4.begin(); it != c4.end(); ++it, ++i) {
    bool val = *it;
    ZX_ASSERT_MSG(p4[i] == val, "Expected %d, got %d", p4[i], val);
  }
}

void TestValuesBool() {
  auto provider = ::zxtest::testing::Bool();
  ZX_ASSERT_MSG(provider.size() == 2, "Provider size should be 2, got %zu.", provider.size());
  ZX_ASSERT_MSG(provider[0] != provider[1], "Bool values should not equal.");
}

void TestRange() {
  auto p1 = ::zxtest::testing::Range(1, 9, 2);
  auto e1 = std::vector<int>({1, 3, 5, 7});
  ZX_ASSERT_MSG(p1.size() == e1.size(), "Resulting provider size: %zu. Expected: %zu", p1.size(),
                e1.size());
  for (size_t i = 0; i < e1.size(); ++i) {
    ZX_ASSERT(p1[i] == e1[i]);
  }

  auto p2 = ::zxtest::testing::Range(1, 2, 2);
  auto e2 = std::vector<int>({1});
  ZX_ASSERT_MSG(p2.size() == e2.size(), "Resulting provider size: %zu. Expected: %zu", p2.size(),
                e2.size());
  for (size_t i = 0; i < e2.size(); ++i) {
    ZX_ASSERT(p2[i] == e2[i]);
  }

  auto p3 = ::zxtest::testing::Range(1, 5);
  auto e3 = std::vector<int>({1, 2, 3, 4});
  ZX_ASSERT_MSG(p3.size() == e3.size(), "Resulting provider size: %zu. Expected: %zu", p3.size(),
                e3.size());
  for (size_t i = 0; i < e3.size(); ++i) {
    ZX_ASSERT(p3[i] == e3[i]);
  }

  auto p4 = ::zxtest::testing::Range(8, 16, 2);
  auto e4 = std::vector<int>({8, 10, 12, 14});
  ZX_ASSERT_MSG(p4.size() == e4.size(), "Resulting provider size: %zu. Expected: %zu", p4.size(),
                e4.size());
  for (size_t i = 0; i < e4.size(); ++i) {
    ZX_ASSERT(p4[i] == e4[i]);
  }

  auto p5 = ::zxtest::testing::Range(8.5, 16.3, 2.5);
  auto e5 = std::vector<double>({8.5, 11, 13.5, 16});
  ZX_ASSERT_MSG(p5.size() == e5.size(), "Resulting provider size: %zu. Expected: %zu", p5.size(),
                e5.size());
  for (size_t i = 0; i < e5.size(); ++i) {
    ZX_ASSERT(p5[i] == e5[i]);
  }

  auto p6 = ::zxtest::testing::Range(7.99, 16.95, 2.98);
  auto e6 = std::vector<double>({7.99, 10.97, 13.95, 16.93});
  ZX_ASSERT_MSG(p6.size() == e6.size(), "Resulting provider size: %zu. Expected: %zu", p6.size(),
                e6.size());
  for (size_t i = 0; i < e6.size(); ++i) {
    ZX_ASSERT(p6[i] == e6[i]);
  }

  auto p7 = ::zxtest::testing::Range(7.99, 9.999);
  auto e7 = std::vector<double>({7.99, 8.99, 9.99});
  ZX_ASSERT_MSG(p7.size() == e7.size(), "Resulting provider size: %zu. Expected: %zu", p7.size(),
                e7.size());
  for (size_t i = 0; i < e7.size(); ++i) {
    ZX_ASSERT(p7[i] == e7[i]);
  }
}

void TestValuesSimilarTypes() {
  // Failure would be a compilation error.
  ::zxtest::internal::ValueProvider<std::string> p1 =
      ::zxtest::testing::Values("A", std::string("B"));
  ::zxtest::internal::ValueProvider<long> p2 = ::zxtest::testing::Values(7, 1l, 5);
}

void TestValuesCombine() {
  // Same type
  auto c1 = ::zxtest::testing::Combine(::zxtest::testing::Values(10, 20, 30),
                                       ::zxtest::testing::Values(15, 25, 35));
  std::vector<std::tuple<int, int>> e1 = {
      std::tuple(10, 15), std::tuple(10, 25), std::tuple(10, 35),
      std::tuple(20, 15), std::tuple(20, 25), std::tuple(20, 35),
      std::tuple(30, 15), std::tuple(30, 25), std::tuple(30, 35),
  };
  ZX_ASSERT_MSG(c1.size() == e1.size(), "Provider size should be %zu, got %zu.", e1.size(),
                c1.size());

  for (size_t i = 0; i < c1.size(); ++i) {
    ZX_ASSERT_MSG(e1[i] == c1[i], "Expected (%d, %d), got (%d, %d)", std::get<0>(e1[i]),
                  std::get<1>(e1[i]), std::get<0>(c1[i]), std::get<1>(c1[i]));
  }

  // Different types
  auto c2 = ::zxtest::testing::Combine(::zxtest::testing::Values(1.1, 2.2, 3.3),
                                       ::zxtest::testing::Values(15, 25, 35));
  std::vector<std::tuple<double, int>> e2 = {
      std::tuple(1.1, 15), std::tuple(1.1, 25), std::tuple(1.1, 35),
      std::tuple(2.2, 15), std::tuple(2.2, 25), std::tuple(2.2, 35),
      std::tuple(3.3, 15), std::tuple(3.3, 25), std::tuple(3.3, 35),
  };
  ZX_ASSERT_MSG(c2.size() == e2.size(), "Provider size should be %zu, got %zu.", e2.size(),
                c2.size());

  for (size_t i = 0; i < c2.size(); ++i) {
    ZX_ASSERT_MSG(e2[i] == c2[i], "Expected (%lf, %d), got (%lf, %d)", std::get<0>(e2[i]),
                  std::get<1>(e2[i]), std::get<0>(c2[i]), std::get<1>(c2[i]));
  }

  // Combine with 3 parameters
  auto c3 = ::zxtest::testing::Combine(::zxtest::testing::Values(1.1, 2.2, 3.3),
                                       ::zxtest::testing::Values(15, 25, 35),
                                       ::zxtest::testing::Values(150, 250, 350));
  std::vector<std::tuple<double, int, int>> e3 = {
      std::tuple(1.1, 15, 150), std::tuple(1.1, 15, 250), std::tuple(1.1, 15, 350),
      std::tuple(1.1, 25, 150), std::tuple(1.1, 25, 250), std::tuple(1.1, 25, 350),
      std::tuple(1.1, 35, 150), std::tuple(1.1, 35, 250), std::tuple(1.1, 35, 350),
      std::tuple(2.2, 15, 150), std::tuple(2.2, 15, 250), std::tuple(2.2, 15, 350),
      std::tuple(2.2, 25, 150), std::tuple(2.2, 25, 250), std::tuple(2.2, 25, 350),
      std::tuple(2.2, 35, 150), std::tuple(2.2, 35, 250), std::tuple(2.2, 35, 350),
      std::tuple(3.3, 15, 150), std::tuple(3.3, 15, 250), std::tuple(3.3, 15, 350),
      std::tuple(3.3, 25, 150), std::tuple(3.3, 25, 250), std::tuple(3.3, 25, 350),
      std::tuple(3.3, 35, 150), std::tuple(3.3, 35, 250), std::tuple(3.3, 35, 350),
  };
  ZX_ASSERT_MSG(c3.size() == e3.size(), "Provider size should be %zu, got %zu.", e3.size(),
                c3.size());

  for (size_t i = 0; i < c3.size(); ++i) {
    ZX_ASSERT_MSG(e3[i] == c3[i], "Expected (%lf, %d, %d), got (%lf, %d, %d)", std::get<0>(e3[i]),
                  std::get<1>(e3[i]), std::get<2>(e3[i]), std::get<0>(c3[i]), std::get<1>(c3[i]),
                  std::get<2>(c3[i]));
  }

  // Combine with 4 parameters
  auto c4 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(1.1, 2.2), ::zxtest::testing::Values(15, 25),
      ::zxtest::testing::Values(150, 250), ::zxtest::testing::Values(105, 205));
  std::vector<std::tuple<double, int, int, int>> e4 = {
      std::tuple(1.1, 15, 150, 105), std::tuple(1.1, 15, 150, 205), std::tuple(1.1, 15, 250, 105),
      std::tuple(1.1, 15, 250, 205), std::tuple(1.1, 25, 150, 105), std::tuple(1.1, 25, 150, 205),
      std::tuple(1.1, 25, 250, 105), std::tuple(1.1, 25, 250, 205), std::tuple(2.2, 15, 150, 105),
      std::tuple(2.2, 15, 150, 205), std::tuple(2.2, 15, 250, 105), std::tuple(2.2, 15, 250, 205),
      std::tuple(2.2, 25, 150, 105), std::tuple(2.2, 25, 150, 205), std::tuple(2.2, 25, 250, 105),
      std::tuple(2.2, 25, 250, 205),
  };
  ZX_ASSERT_MSG(c4.size() == e4.size(), "Provider size should be %zu, got %zu.", e4.size(),
                c4.size());

  for (size_t i = 0; i < c4.size(); ++i) {
    ZX_ASSERT_MSG(e4[i] == c4[i], "Expected (%lf, %d, %d, %d), got (%lf, %d, %d, %d)",
                  std::get<0>(e4[i]), std::get<1>(e4[i]), std::get<2>(e4[i]), std::get<3>(e4[i]),
                  std::get<0>(c4[i]), std::get<1>(c4[i]), std::get<2>(c4[i]), std::get<3>(c4[i]));
  }
}

void TestTuplesCombine() {
  // Both tuples
  auto c1 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(std::tuple(10, 11), std::tuple(20, 21), std::tuple(30, 31)),
      ::zxtest::testing::Values(std::tuple(15, 16), std::tuple(25, 26), std::tuple(35, 36)));
  std::vector<std::tuple<std::tuple<int, int>, std::tuple<int, int>>> e1 = {
      std::tuple(std::tuple(10, 11), std::tuple(15, 16)),
      std::tuple(std::tuple(10, 11), std::tuple(25, 26)),
      std::tuple(std::tuple(10, 11), std::tuple(35, 36)),
      std::tuple(std::tuple(20, 21), std::tuple(15, 16)),
      std::tuple(std::tuple(20, 21), std::tuple(25, 26)),
      std::tuple(std::tuple(20, 21), std::tuple(35, 36)),
      std::tuple(std::tuple(30, 31), std::tuple(15, 16)),
      std::tuple(std::tuple(30, 31), std::tuple(25, 26)),
      std::tuple(std::tuple(30, 31), std::tuple(35, 36)),
  };
  ZX_ASSERT_MSG(c1.size() == e1.size(), "Provider size should be %zu, got %zu.", e1.size(),
                c1.size());

  size_t c_size = std::tuple_size<std::remove_reference<decltype(c1[0])>::type>::value;
  size_t e_size = std::tuple_size<std::remove_reference<decltype(e1[0])>::type>::value;
  ZX_ASSERT_MSG(c_size == e_size, "Size is wrong: got %zu, expected %zu", c_size, e_size);

  for (size_t i = 0; i < c1.size(); ++i) {
    ZX_ASSERT(std::get<0>(e1[i]) == std::get<0>(c1[i]));
    ZX_ASSERT(std::get<1>(e1[i]) == std::get<1>(c1[i]));
  }

  // First tuple only
  auto c2 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(std::tuple(10, 11), std::tuple(20, 21), std::tuple(30, 31)),
      ::zxtest::testing::Values(15, 25, 35));
  std::vector<std::tuple<std::tuple<int, int>, int>> e2 = {
      std::tuple(std::tuple(10, 11), 15), std::tuple(std::tuple(10, 11), 25),
      std::tuple(std::tuple(10, 11), 35), std::tuple(std::tuple(20, 21), 15),
      std::tuple(std::tuple(20, 21), 25), std::tuple(std::tuple(20, 21), 35),
      std::tuple(std::tuple(30, 31), 15), std::tuple(std::tuple(30, 31), 25),
      std::tuple(std::tuple(30, 31), 35),
  };
  ZX_ASSERT_MSG(c2.size() == e2.size(), "Provider size should be %zu, got %zu.", e2.size(),
                c2.size());

  c_size = std::tuple_size<std::remove_reference<decltype(c2[0])>::type>::value;
  e_size = std::tuple_size<std::remove_reference<decltype(e2[0])>::type>::value;
  ZX_ASSERT_MSG(c_size == e_size, "Size is wrong: got %zu, expected %zu", c_size, e_size);

  for (size_t i = 0; i < c2.size(); ++i) {
    ZX_ASSERT(std::get<0>(e2[i]) == std::get<0>(c2[i]));
    ZX_ASSERT(std::get<1>(e2[i]) == std::get<1>(c2[i]));
  }

  // Second tuple only
  auto c3 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(10, 20, 30),
      ::zxtest::testing::Values(std::tuple(15, 16), std::tuple(25, 26), std::tuple(35, 36)));
  std::vector<std::tuple<int, std::tuple<int, int>>> e3 = {
      std::tuple(10, std::tuple(15, 16)), std::tuple(10, std::tuple(25, 26)),
      std::tuple(10, std::tuple(35, 36)), std::tuple(20, std::tuple(15, 16)),
      std::tuple(20, std::tuple(25, 26)), std::tuple(20, std::tuple(35, 36)),
      std::tuple(30, std::tuple(15, 16)), std::tuple(30, std::tuple(25, 26)),
      std::tuple(30, std::tuple(35, 36)),
  };
  ZX_ASSERT_MSG(c3.size() == e3.size(), "Provider size should be %zu, got %zu.", e3.size(),
                c3.size());

  c_size = std::tuple_size<std::remove_reference<decltype(c3[0])>::type>::value;
  e_size = std::tuple_size<std::remove_reference<decltype(e3[0])>::type>::value;
  ZX_ASSERT_MSG(c_size == e_size, "Size is wrong: got %zu, expected %zu", c_size, e_size);

  for (size_t i = 0; i < c3.size(); ++i) {
    ZX_ASSERT(std::get<0>(e3[i]) == std::get<0>(c3[i]));
    ZX_ASSERT(std::get<1>(e3[i]) == std::get<1>(c3[i]));
  }

  // Quad tuples
  auto c4 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(std::tuple(10, 11), std::tuple(20, 21)),
      ::zxtest::testing::Values(std::tuple(15, 16), std::tuple(25, 26)),
      ::zxtest::testing::Values(std::tuple(1.5, 1.6), std::tuple(2.5, 2.6)),
      ::zxtest::testing::Values(std::tuple("a", "b")));
  std::vector<std::tuple<std::tuple<int, int>, std::tuple<int, int>, std::tuple<double, double>,
                         std::tuple<const char*, const char*>>>
      e4 = {
          std::tuple(std::tuple(10, 11), std::tuple(15, 16), std::tuple(1.5, 1.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(15, 16), std::tuple(2.5, 2.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(25, 26), std::tuple(1.5, 1.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(25, 26), std::tuple(2.5, 2.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(15, 16), std::tuple(1.5, 1.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(15, 16), std::tuple(2.5, 2.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(25, 26), std::tuple(1.5, 1.6),
                     std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(25, 26), std::tuple(2.5, 2.6),
                     std::tuple("a", "b")),
      };
  ZX_ASSERT_MSG(c4.size() == e4.size(), "Provider size should be %zu, got %zu.", e4.size(),
                c4.size());

  c_size = std::tuple_size<std::remove_reference<decltype(c4[0])>::type>::value;
  e_size = std::tuple_size<std::remove_reference<decltype(e4[0])>::type>::value;
  ZX_ASSERT_MSG(c_size == e_size, "Size is wrong: got %zu, expected %zu", c_size, e_size);

  for (size_t i = 0; i < c4.size(); ++i) {
    ZX_ASSERT(std::get<0>(e4[i]) == std::get<0>(c4[i]));
    ZX_ASSERT(std::get<1>(e4[i]) == std::get<1>(c4[i]));
    ZX_ASSERT(std::get<2>(e4[i]) == std::get<2>(c4[i]));
    ZX_ASSERT(std::get<3>(e4[i]) == std::get<3>(c4[i]));
  }

  // Mixed
  auto c5 = ::zxtest::testing::Combine(
      ::zxtest::testing::Values(std::tuple(10, 11), std::tuple(20, 21)),
      ::zxtest::testing::Values(std::tuple(15, 16), std::tuple(25, 26)),
      ::zxtest::testing::Values(1.5, 2.5), ::zxtest::testing::Values(std::tuple("a", "b")));
  std::vector<std::tuple<std::tuple<int, int>, std::tuple<int, int>, double,
                         std::tuple<const char*, const char*>>>
      e5 = {
          std::tuple(std::tuple(10, 11), std::tuple(15, 16), 1.5, std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(15, 16), 2.5, std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(25, 26), 1.5, std::tuple("a", "b")),
          std::tuple(std::tuple(10, 11), std::tuple(25, 26), 2.5, std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(15, 16), 1.5, std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(15, 16), 2.5, std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(25, 26), 1.5, std::tuple("a", "b")),
          std::tuple(std::tuple(20, 21), std::tuple(25, 26), 2.5, std::tuple("a", "b")),
      };
  ZX_ASSERT_MSG(c5.size() == e5.size(), "Provider size should be %zu, got %zu.", e5.size(),
                c5.size());

  c_size = std::tuple_size<std::remove_reference<decltype(c5[0])>::type>::value;
  e_size = std::tuple_size<std::remove_reference<decltype(e5[0])>::type>::value;
  ZX_ASSERT_MSG(c_size == e_size, "Size is wrong: got %zu, expected %zu", c_size, e_size);

  for (size_t i = 0; i < c5.size(); ++i) {
    ZX_ASSERT(std::get<0>(e5[i]) == std::get<0>(c5[i]));
    ZX_ASSERT(std::get<1>(e5[i]) == std::get<1>(c5[i]));
    ZX_ASSERT(std::get<2>(e5[i]) == std::get<2>(c5[i]));
    ZX_ASSERT(std::get<3>(e5[i]) == std::get<3>(c5[i]));
  }
}

}  // namespace zxtest::test
