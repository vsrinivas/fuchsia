// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZXTEST_BASE_VALUES_H_
#define ZXTEST_BASE_VALUES_H_

#include <lib/stdcompat/array.h>
#include <lib/stdcompat/span.h>
#include <math.h>

#include <tuple>
#include <type_traits>
#include <vector>

#include <fbl/function.h>
#include <zxtest/base/test.h>

namespace zxtest {
namespace internal {

// This class takes provides a container interface but is based on taking a lambda that owns the
// storage of the parameters and provides access to them. The contained parameters are immutable.
template <typename T>
class ValueProvider {
 public:
  using ValueType = std::remove_cv_t<T>;
  using ConstRef = const ValueType&;
  using Callback = typename fbl::Function<ConstRef(size_t)>;

  ValueProvider() = delete;
  ValueProvider(Callback accessor, size_t size) : accessor_(std::move(accessor)), size_(size) {}
  ValueProvider(const ValueProvider&) = delete;
  ValueProvider(ValueProvider&&) noexcept = default;
  template <typename U, typename std::enable_if_t<std::is_convertible_v<U, T> &&
                                                  !std::is_same_v<U, T>>* = nullptr>
  ValueProvider(ValueProvider<U>&& other)
      : accessor_([cb = std::move(other.accessor_)](size_t index) -> const T& {
          static T tmp;
          tmp = cb(index);
          return tmp;
        }),
        size_(other.size_) {}
  ValueProvider& operator=(ValueProvider&&) noexcept = default;
  ~ValueProvider() = default;

  ConstRef operator[](size_t index) const {
    ZX_ASSERT_MSG(index < size_, "Out of range.");
    return accessor_(index);
  }

  size_t size() const { return size_; }

 private:
  template <typename U>
  friend class ValueProvider;

  Callback accessor_ = nullptr;
  size_t size_ = 0;
};
}  // namespace internal

namespace testing {
namespace internal {

// NOTE about `Combine`:
// We handle more than two parameters through recursion. The leftmost two parameters are combined
// into a tuple at each recursive call, and the tuples are merged using `std::tuple_cat`. However,
// if the user wants to pass in a tuple parameter, that must be handled like a regular value and not
// be concatenated using `std::tuple_cat`. The combination of `Combine` and `internal::Combine`
// functions handle this distinction.

// Internal combine handler.
// See above for a note about why there are internal handlers.
template <typename... A, typename B>
auto Combine(::zxtest::internal::ValueProvider<std::tuple<A...>> a,
             ::zxtest::internal::ValueProvider<B> b) {
  using ParamType = decltype(std::tuple_cat(a[0], std::tuple(b[0])));
  size_t total_elements = a.size() * b.size();
  auto values = [a = std::move(a), b = std::move(b)](size_t index) -> ParamType& {
    size_t a_index = index / b.size();
    size_t b_index = index % b.size();

    static ParamType storage;
    storage = std::tuple_cat(a[a_index], std::tuple(b[b_index]));
    return storage;
  };
  return ::zxtest::internal::ValueProvider<ParamType>(
      [a = std::move(values)](size_t index) -> const ParamType& { return a(index); },
      total_elements);
}

// Internal combine handler.
// See above for a note about why there are internal handlers.
template <typename... A, typename... B>
auto Combine(::zxtest::internal::ValueProvider<std::tuple<A...>> a,
             ::zxtest::internal::ValueProvider<std::tuple<B...>> b) {
  using ParamType = decltype(std::tuple_cat(a[0], std::make_tuple(b[0])));
  size_t total_elements = a.size() * b.size();
  auto values = [a = std::move(a), b = std::move(b)](size_t index) -> ParamType& {
    size_t a_index = index / b.size();
    size_t b_index = index % b.size();

    static ParamType storage;
    storage = std::tuple_cat(a[a_index], std::make_tuple(b[b_index]));
    return storage;
  };
  return ::zxtest::internal::ValueProvider<ParamType>(
      [a = std::move(values)](size_t index) -> const ParamType& { return a(index); },
      total_elements);
}

// Internal combine handler.
// See above for a note about why there are internal handlers.
template <typename A, typename B, typename... ProviderType>
auto Combine(::zxtest::internal::ValueProvider<A> a, ::zxtest::internal::ValueProvider<B> b,
             ProviderType&&... providers) {
  return internal::Combine(std::move(internal::Combine(std::move(a), std::move(b))),
                           std::forward<ProviderType>(providers)...);
}
}  // namespace internal

// Combines two ValueProviders producing a ValueProvider with a cartesian product of both.
template <typename A, typename B>
auto Combine(::zxtest::internal::ValueProvider<A> a, ::zxtest::internal::ValueProvider<B> b) {
  using ParamType = typename std::tuple<A, B>;
  size_t total_elements = a.size() * b.size();
  auto values = [a = std::move(a), b = std::move(b)](size_t index) -> ParamType& {
    size_t a_index = index / b.size();
    size_t b_index = index % b.size();

    static ParamType storage;
    storage = std::tuple(a[a_index], b[b_index]);
    return storage;
  };
  return ::zxtest::internal::ValueProvider<ParamType>(
      [a = std::move(values)](size_t index) -> const ParamType& { return a(index); },
      total_elements);
}

// Combines more than two ValueProviders.
template <typename A, typename B, typename... ProviderType>
auto Combine(::zxtest::internal::ValueProvider<A> a, ::zxtest::internal::ValueProvider<B> b,
             ProviderType&&... providers) {
  return internal::Combine(std::move(zxtest::testing::Combine(std::move(a), std::move(b))),
                           std::forward<ProviderType>(providers)...);
}

// Takes in a container of values and returns a ValueProvider for those values.
// Supports containers that support ::value_type and iterator.
template <typename C>
auto ValuesIn(const C& values) {
  using ParamType = typename C::value_type;
  size_t size = values.size();
  return ::zxtest::internal::ValueProvider<ParamType>(
      [values = std::move(values)](size_t index) -> const ParamType& {
        return *(values.begin() + index);
      },
      size);
}

// Takes in values as parameters and returns a ValueProvider for those values.
template <typename... Args>
auto Values(Args... args) {
  using ParamType = typename std::common_type<Args...>::type;
  auto values = cpp20::to_array<ParamType>({args...});
  return ValuesIn(values);
}

// Generates a series of values according to the parameters given.
// Increments by `step` starting from `start`, ending before `end`.
// The `end` value is excluded.
template <typename A, typename B, typename T>
auto Range(A start, B end, T step) {
  static_assert((std::is_same_v<A, B>), "`start` and `end` parameters must be of equal type.");
  ZX_ASSERT_MSG(start < end, "`start` must be less than `end`.");
  A gap = end - start;
  size_t size = static_cast<size_t>(ceil(static_cast<double>(gap) / static_cast<double>(step)));
  return ::zxtest::internal::ValueProvider<A>(
      [start = std::move(start), step = std::move(step)](size_t index) -> const A& {
        static A result;
        // Cast to `A` is safe because `index` is already enforced to be smaller than `size` in the
        // `ValueProvider`.
        result = start + (step * static_cast<A>(index));
        return result;
      },
      size);
}

template <typename A, typename B>
auto Range(A start, B end) {
  return Range(start, end, 1);
}

// Helper function to return a ValueProvider that has both bool values.
static inline auto Bool() { return Values(false, true); }
}  // namespace testing
}  // namespace zxtest

#endif  // ZXTEST_BASE_VALUES_H_
