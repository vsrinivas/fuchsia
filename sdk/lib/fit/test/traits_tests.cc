// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/fit/traits.h>

#include <functional>

#include <zxtest/zxtest.h>

namespace {

namespace test_callables {
template <typename Callable, typename... Args>
void invoke_with_defaults(Callable c, fit::parameter_pack<Args...>) {
  c(Args()...);
}

template <typename Callable>
void invoke_with_defaults(Callable c) {
  invoke_with_defaults(std::move(c), typename fit::callable_traits<Callable>::args{});
}

TEST(TraitsTest, arg_capture) {
  int i = 0;
  invoke_with_defaults([&] { i = 42; });
  EXPECT_EQ(42, i);
  invoke_with_defaults([&](int, float) { i = 54; });
  EXPECT_EQ(54, i);
}

// Performs static assertions against a function-like type of signature int(float, bool).
template <typename Callable>
struct CallableTraitsTest {
  using Traits = fit::callable_traits<Callable>;

  static_assert(std::is_same_v<int(float, bool), typename Traits::signature>, "");
  static_assert(std::is_same_v<int, typename Traits::return_type>, "");
  static_assert(2 == Traits::args::size, "");
  static_assert(std::is_same_v<float, typename Traits::args::template at<0>>, "");
  static_assert(std::is_same_v<bool, typename Traits::args::template at<1>>, "");
};

// compile-time test
namespace signature_traits {
using Traits = CallableTraitsTest<int(float, bool)>::Traits;
}  // namespace signature_traits

// compile-time test
namespace function_pointer_traits {
using Traits = CallableTraitsTest<int (*)(float, bool)>::Traits;
static_assert(std::is_same_v<int (*)(float, bool), Traits::type>, "");
}  // namespace function_pointer_traits

// compile-time test
namespace member_function_pointer_traits {
struct Object {
  int method(float, bool);
};
using Traits = CallableTraitsTest<int (Object::*)(float, bool)>::Traits;
static_assert(std::is_same_v<Object, Traits::type>, "");
}  // namespace member_function_pointer_traits

// compile-time test
namespace lambda_traits {
[[maybe_unused]] auto lambda = [](float, bool) { return 0; };
using Traits = CallableTraitsTest<decltype(lambda)>::Traits;
}  // namespace lambda_traits

template <typename Functor>
struct FunctorTraitsTest {
  using Traits = typename CallableTraitsTest<Functor>::Traits;
  static_assert(std::is_same_v<Functor, typename Traits::type>, "");
};

// compile-time test
namespace mutable_functor_traits {
struct MutableFunctor {
  int operator()(float, bool) { return 0; }
};
using Traits = FunctorTraitsTest<MutableFunctor>::Traits;
}  // namespace mutable_functor_traits

// compile-time test
namespace fit_callable_traits {
using Traits = FunctorTraitsTest<fit::function<int(float, bool)>>;
}  // namespace fit_callable_traits

// compile-time test
namespace std_callable_traits {
using Traits = FunctorTraitsTest<std::function<int(float, bool)>>;
}  // namespace std_callable_traits

static_assert(!fit::is_callable<void>::value, "");
static_assert(!fit::is_callable<int>::value, "");
static_assert(!fit::is_callable<int(float, bool)>::value, "");
static_assert(fit::is_callable<int (*)(float, bool)>::value, "");
static_assert(fit::is_callable<int (member_function_pointer_traits::Object::*)(float, bool)>::value,
              "");
static_assert(fit::is_callable<decltype(lambda_traits::lambda)>::value, "");
static_assert(fit::is_callable<mutable_functor_traits::MutableFunctor>::value, "");

static_assert(fit::is_callable<fit::function<int(float, bool)>>::value, "");
static_assert(fit::is_callable<fit::callback<int(float, bool)>>::value, "");

static_assert(fit::is_callable<std::function<int(float, bool)>>::value, "");

}  // namespace test_callables

namespace test_detection {

struct Nothing {};

template <typename T>
struct OneSpecialization {};

template <>
struct OneSpecialization<int> {
  using type = void;
};

template <typename T>
using only_int_t = typename OneSpecialization<T>::type;

template <typename T>
using arithmetic_exists_t = std::enable_if_t<cpp17::is_arithmetic_v<T>, std::add_const_t<T>>;

struct HasEqualityBad {
  void operator==(const HasEqualityBad&) const {}
};

struct HasEqualityExact {
  bool operator==(const HasEqualityExact&) const { return true; }
};

struct HasEqualityConvertible {
  int operator==(const HasEqualityConvertible&) const { return 1; }
};

template <typename T>
using equality_t = decltype(std::declval<const T&>() == std::declval<const T&>());

template <typename T>
constexpr bool has_equality_v = fit::is_detected_v<equality_t, T>;

template <typename T>
constexpr bool has_equality_exact_v = fit::is_detected_exact_v<bool, equality_t, T>;

template <typename T>
constexpr bool has_equality_convertible_v = fit::is_detected_convertible_v<bool, equality_t, T>;

static_assert(!fit::is_detected_v<only_int_t, void>, "");
static_assert(!fit::is_detected_v<only_int_t, Nothing>, "");
static_assert(fit::is_detected_v<only_int_t, int>, "");

static_assert(!fit::is_detected_v<arithmetic_exists_t, void>, "");
static_assert(!fit::is_detected_v<arithmetic_exists_t, Nothing>, "");
static_assert(fit::is_detected_v<arithmetic_exists_t, int>, "");
static_assert(fit::is_detected_v<arithmetic_exists_t, float>, "");

static_assert(cpp17::is_same_v<fit::detected_t<only_int_t, void>, fit::nonesuch>, "");
static_assert(cpp17::is_same_v<fit::detected_t<only_int_t, int>, void>, "");
static_assert(cpp17::is_same_v<fit::detected_t<arithmetic_exists_t, int>, const int>, "");
static_assert(cpp17::is_same_v<fit::detected_t<arithmetic_exists_t, float>, const float>, "");

static_assert(cpp17::is_same_v<fit::detected_or_t<bool, only_int_t, void>, bool>, "");
static_assert(cpp17::is_same_v<fit::detected_or_t<bool, only_int_t, int>, void>, "");

static_assert(!has_equality_v<void>, "");
static_assert(!has_equality_v<Nothing>, "");
static_assert(has_equality_v<HasEqualityBad>, "");
static_assert(has_equality_v<HasEqualityExact>, "");
static_assert(has_equality_v<HasEqualityConvertible>, "");
static_assert(has_equality_v<int>, "");

static_assert(!has_equality_exact_v<void>, "");
static_assert(!has_equality_exact_v<Nothing>, "");
static_assert(!has_equality_exact_v<HasEqualityBad>, "");
static_assert(has_equality_exact_v<HasEqualityExact>, "");
static_assert(!has_equality_exact_v<HasEqualityConvertible>, "");
static_assert(has_equality_exact_v<int>, "");

static_assert(!has_equality_convertible_v<void>, "");
static_assert(!has_equality_convertible_v<Nothing>, "");
static_assert(!has_equality_convertible_v<HasEqualityBad>, "");
static_assert(has_equality_convertible_v<HasEqualityExact>, "");
static_assert(has_equality_convertible_v<HasEqualityConvertible>, "");
static_assert(has_equality_convertible_v<int>, "");

}  // namespace test_detection

}  // namespace
