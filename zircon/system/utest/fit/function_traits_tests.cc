// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <lib/fit/function.h>
#include <lib/fit/function_traits.h>
#include <unittest/unittest.h>

namespace {

template <typename Callable, typename... Args>
void invoke_with_defaults(Callable c, fit::parameter_pack<Args...>) {
  c(Args()...);
}

template <typename Callable>
void invoke_with_defaults(Callable c) {
  invoke_with_defaults(std::move(c), typename fit::function_traits<Callable>::args{});
}

bool arg_capture() {
  BEGIN_TEST;

  int i = 0;
  invoke_with_defaults([&] { i = 42; });
  EXPECT_EQ(42, i);
  invoke_with_defaults([&](int, float) { i = 54; });
  EXPECT_EQ(54, i);

  END_TEST;
}

// Performs static assertions against a function-like type of signature int(float, bool).
template <typename Callable>
struct TraitsTest {
  using Traits = fit::function_traits<Callable>;

  static_assert(std::is_same_v<int(float, bool), typename Traits::signature>);
  static_assert(std::is_same_v<int, typename Traits::return_type>);
  static_assert(2 == Traits::args::size);
  static_assert(std::is_same_v<float, typename Traits::args::template at<0>>);
  static_assert(std::is_same_v<bool, typename Traits::args::template at<1>>);
};

// compile-time test
namespace signature_traits {
using Traits = TraitsTest<int(float, bool)>::Traits;
}  // namespace signature_traits

// compile-time test
namespace function_pointer_traits {
using Traits = TraitsTest<int (*)(float, bool)>::Traits;
static_assert(std::is_same_v<int (*)(float, bool), Traits::type>);
}  // namespace function_pointer_traits

// compile-time test
namespace lambda_traits {
auto lambda = [](float, bool) { return 0; };
using Traits = TraitsTest<decltype(lambda)>::Traits;
}  // namespace lambda_traits

template <typename Functor>
struct FunctorTraitsTest {
  using Traits = typename TraitsTest<Functor>::Traits;
  static_assert(std::is_same_v<Functor, typename Traits::type>);
};

// compile-time test
namespace mutable_functor_traits {
struct MutableFunctor {
  int operator()(float, bool) { return 0; }
};
using Traits = FunctorTraitsTest<MutableFunctor>::Traits;
}  // namespace mutable_functor_traits

// compile-time test
namespace fit_function_traits {
using Traits = FunctorTraitsTest<fit::function<int(float, bool)>>;
}  // namespace fit_function_traits

// compile-time test
namespace std_function_traits {
using Traits = FunctorTraitsTest<std::function<int(float, bool)>>;
}  // namespace std_function_traits

}  // namespace

BEGIN_TEST_CASE(function_traits_tests)
RUN_TEST(arg_capture)
// suppress -Wunneeded-internal-declaration
(void)lambda_traits::lambda;
END_TEST_CASE(function_traits_tests)
