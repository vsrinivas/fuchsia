// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_TESTING_LOADBENCH_RANDOM_H_
#define SRC_TESTING_LOADBENCH_RANDOM_H_

#include <limits>
#include <random>
#include <type_traits>
#include <utility>

// Utility to simplify random number generation and item selection on top of the
// STD random library.
class Random {
 public:
  Random() = default;

  // Trait to determine the underlying integer type of a given type T. Supports
  // integral types and enumerations. Additional specializations may be added to
  // support other countable or enumerable objects.
  template <typename T, typename = void>
  struct BaseType {
    using Type = T;
  };
  template <typename Enum>
  struct BaseType<Enum, std::enable_if_t<std::is_enum<Enum>::value>> {
    using Type = std::underlying_type_t<Enum>;
  };
  template <typename T>
  using Base = typename BaseType<T>::Type;

  template <typename T>
  T GetUniform() {
    std::uniform_int_distribution<Base<T>> distribution{std::numeric_limits<Base<T>>::lowest(),
                                                        std::numeric_limits<Base<T>>::max()};
    return static_cast<T>(distribution(generator_));
  }

  template <typename T>
  T GetUniform(T min, T max) {
    std::uniform_int_distribution<Base<T>> distribution{static_cast<Base<T>>(min),
                                                        static_cast<Base<T>>(max)};
    return static_cast<T>(distribution(generator_));
  }

  template <typename T>
  T SelectUniform(std::initializer_list<T> il) {
    std::uniform_int_distribution<std::size_t> distribution{0, il.size() > 0 ? il.size() - 1 : 0};
    return il.begin()[distribution(generator_)];
  }

 private:
  std::mt19937_64 generator_{std::random_device{}()};
};

#endif  // SRC_TESTING_LOADBENCH_RANDOM_H_
