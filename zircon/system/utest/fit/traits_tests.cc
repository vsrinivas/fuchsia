// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <functional>

#include <lib/fit/function.h>
#include <lib/fit/traits.h>
#include <unittest/unittest.h>

namespace {

namespace test_void_t {
static_assert(std::is_same<fit::void_t<>, void>::value, "");
static_assert(std::is_same<fit::void_t<int>, void>::value, "");
static_assert(std::is_same<fit::void_t<int, int>, void>::value, "");
}  // namespace test_void_t

namespace test_logical_traits {
static_assert(fit::conjunction_v<> == true, "");
static_assert(fit::conjunction_v<std::false_type> == false, "");
static_assert(fit::conjunction_v<std::true_type> == true, "");
static_assert(fit::conjunction_v<std::false_type, std::false_type> == false, "");
static_assert(fit::conjunction_v<std::false_type, std::true_type> == false, "");
static_assert(fit::conjunction_v<std::true_type, std::false_type> == false, "");
static_assert(fit::conjunction_v<std::true_type, std::true_type> == true, "");

static_assert(fit::disjunction_v<> == false, "");
static_assert(fit::disjunction_v<std::false_type> == false, "");
static_assert(fit::disjunction_v<std::true_type> == true, "");
static_assert(fit::disjunction_v<std::false_type, std::false_type> == false, "");
static_assert(fit::disjunction_v<std::false_type, std::true_type> == true, "");
static_assert(fit::disjunction_v<std::true_type, std::false_type> == true, "");
static_assert(fit::disjunction_v<std::true_type, std::true_type> == true, "");

static_assert(fit::negation_v<std::false_type> == true, "");
static_assert(fit::negation_v<std::true_type> == false, "");
}  // namespace test_logical_traits

namespace test_callables {
template <typename Callable, typename... Args>
void invoke_with_defaults(Callable c, fit::parameter_pack<Args...>) {
  c(Args()...);
}

template <typename Callable>
void invoke_with_defaults(Callable c) {
  invoke_with_defaults(std::move(c), typename fit::callable_traits<Callable>::args{});
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
auto lambda = [](float, bool) { return 0; };
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

}  // namespace

BEGIN_TEST_CASE(traits_test)
RUN_TEST(test_callables::arg_capture)
// suppress -Wunneeded-internal-declaration
(void)test_callables::lambda_traits::lambda;
END_TEST_CASE(traits_test)
