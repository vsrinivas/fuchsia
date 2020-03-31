// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FITX_INTERNAL_RESULT_H_
#define LIB_FITX_INTERNAL_RESULT_H_

#include <lib/fitx/internal/type_traits.h>

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

namespace fitx {

// Forward declaration.
template <typename E, typename... Ts>
class result;

// Forward declarations.
template <typename E>
class error;

template <typename... Ts>
class success;

namespace internal {

// Determines whether T has an operator-> overload and provides a method that
// forwards its argument by reference when T has the overload, or by pointer
// otherwise.
template <typename T, typename = void>
struct arrow_operator {
  static constexpr T* forward(T& value) {
    return &value;
  }
  static constexpr const T* forward(const T& value) {
    return &value;
  }
};
template <typename T>
struct arrow_operator<T, void_t<decltype(std::declval<T>().operator->())>> {
  static constexpr T& forward(T& value) {
    return value;
  }
  static constexpr const T& forward(const T& value) {
    return value;
  }
};

// Detects whether the given expression evaluates to a fitx::result.
template <typename E, typename... Ts>
std::true_type match_result(const ::fitx::result<E, Ts...>&);
std::false_type match_result(...);

// Predicate indicating whether type T is an instantiation of fitx::result.
template <typename T>
static constexpr bool is_result = decltype(match_result(std::declval<T>()))::value;

// Predicate indicating whether type T is not an instantiation of fitx::result.
template <typename T>
struct not_result_type : bool_constant<!is_result<T>> {};

// Detects whether the given expression evaluates to a fitx::error.
template <typename E>
std::true_type match_error(const ::fitx::error<E>&);
std::false_type match_error(...);

// Predicate indicating whether type T is an instantiation of fitx::error.
template <typename T>
static constexpr bool is_error = decltype(match_error(std::declval<T>()))::value;

// Detects whether the given expression evaluates to a fitx::ok.
template <typename... Ts>
std::true_type match_success(const ::fitx::success<Ts...>&);
std::false_type match_success(...);

// Predicate indicating whether type T is an instantiation of fitx::ok.
template <typename T>
static constexpr bool is_success = decltype(match_success(std::declval<T>()))::value;

// Predicate indicating whether type T is not an instantiation of fitx::error.
template <typename T>
struct not_error_type : bool_constant<!is_error<T>> {};

// Concept helper for constructor, method, and operator overloads.
template <typename... Conditions>
using requires_conditions = std::enable_if_t<conjunction_v<Conditions...>, bool>;

// Enable if relational operator is convertible to bool and the optional
// conditions are true.
template <typename Op, typename... Conditions>
using enable_rel_op =
    std::enable_if_t<(std::is_convertible<Op, bool>::value && conjunction_v<Conditions...>), bool>;

// Specifies whether a type is trivially or non-trivially destructible.
enum class storage_class_e {
  trivial,
  non_trivial,
};

// Evaluates to storage_class_e::trivial if all of the types in Ts are trivially
// destructible, storage_class_e::non_trivial otherwise.
template <typename... Ts>
static constexpr storage_class_e storage_class_trait =
    conjunction_v<std::is_trivially_destructible<Ts>...> ? storage_class_e::trivial
                                                         : storage_class_e::non_trivial;

// Trivial type for the default variant of the union below.
struct empty_type {};

// Type tags to discriminate between empty, error, and value constructors,
// avoiding ambiguity with copy/move constructors.
enum empty_t { empty_v };
enum error_t { error_v };
enum value_t { value_v };

// Union that stores either nothing, an error of type E, or a value of type T.
// This type is specialized for trivially and non-trivially destructible types
// to support multi-register return values for trivial types.
template <typename E, typename T, storage_class_e = storage_class_trait<E, T>>
union error_or_value_type {
  constexpr error_or_value_type() : empty{} {}

  constexpr error_or_value_type(const error_or_value_type&) = default;
  constexpr error_or_value_type& operator=(const error_or_value_type&) = default;
  constexpr error_or_value_type(error_or_value_type&&) = default;
  constexpr error_or_value_type& operator=(error_or_value_type&&) = default;

  template <typename F>
  constexpr error_or_value_type(error_t, F&& error) : error(std::forward<F>(error)) {}

  template <typename U>
  constexpr error_or_value_type(value_t, U&& value) : value(std::forward<U>(value)) {}

  ~error_or_value_type() = default;

  constexpr void destroy(error_t) {}
  constexpr void destroy(value_t) {}

  empty_type empty;
  E error;
  T value;
};
template <typename E, typename T>
union error_or_value_type<E, T, storage_class_e::non_trivial> {
  constexpr error_or_value_type() : empty{} {}

  constexpr error_or_value_type(const error_or_value_type&) = default;
  constexpr error_or_value_type& operator=(const error_or_value_type&) = default;
  constexpr error_or_value_type(error_or_value_type&&) = default;
  constexpr error_or_value_type& operator=(error_or_value_type&&) = default;

  template <typename F>
  constexpr error_or_value_type(error_t, F&& error) : error(std::forward<F>(error)) {}

  template <typename U>
  constexpr error_or_value_type(value_t, U&& value) : value(std::forward<U>(value)) {}

  ~error_or_value_type() {}

  constexpr void destroy(error_t) { error.E::~E(); }
  constexpr void destroy(value_t) { value.T::~T(); }

  empty_type empty;
  E error;
  T value;
};

// Specifies whether the storage is empty, contains an error, or contains a
// a value.
enum class state_e {
  empty,
  has_error,
  has_value,
};

// Storage type is either empty, holds an error, or holds a set of values. This
// type is specialized for trivially and non-trivially destructible types. When
// E and all of the elements of Ts are trivially destructible, this type
// provides a trivial destructor, which is necessary for multi-register return
// value optimization.
template <storage_class_e storage_class, typename E, typename... Ts>
struct storage_type;

template <storage_class_e storage_class, typename E, typename T>
struct storage_type<storage_class, E, T> {
  using value_type = error_or_value_type<E, T>;

  constexpr storage_type() = default;

  constexpr storage_type(const storage_type&) = default;
  constexpr storage_type& operator=(const storage_type&) = default;
  constexpr storage_type(storage_type&&) = default;
  constexpr storage_type& operator=(storage_type&&) = default;

  constexpr void destroy() {}

  constexpr void reset() { state = state_e::empty; }

  ~storage_type() = default;

  explicit constexpr storage_type(empty_t) {}

  template <typename F>
  constexpr storage_type(error_t, F&& error)
      : state{state_e::has_error}, error_or_value{error_v, std::forward<F>(error)} {}

  template <typename U>
  explicit constexpr storage_type(value_t, U&& value)
      : state{state_e::has_value}, error_or_value{value_v, std::forward<U>(value)} {}

  template <storage_class_e other_storage_class, typename F, typename U>
  explicit constexpr storage_type(storage_type<other_storage_class, F, U>&& other)
      : state{other.state},
        error_or_value{other.state == state_e::empty
                           ? value_type{}
                           : other.state == state_e::has_error
                                 ? value_type{error_v, std::move(other.error_or_value.error)}
                                 : value_type{value_v, std::move(other.error_or_value.value)}} {}

  state_e state{state_e::empty};
  value_type error_or_value;
};
template <typename E, typename T>
struct storage_type<storage_class_e::non_trivial, E, T> {
  using value_type = error_or_value_type<E, T>;

  constexpr storage_type() = default;

  constexpr storage_type(const storage_type&) = default;
  constexpr storage_type& operator=(const storage_type&) = default;
  constexpr storage_type(storage_type&&) = default;
  constexpr storage_type& operator=(storage_type&&) = default;

  constexpr void destroy() {
    if (state == state_e::has_value) {
      error_or_value.destroy(value_v);
    } else if (state == state_e::has_error) {
      error_or_value.destroy(error_v);
    }
  }

  constexpr void reset() {
    destroy();
    state = state_e::empty;
  }

  ~storage_type() { destroy(); }

  explicit constexpr storage_type(empty_t) {}

  template <typename F>
  constexpr storage_type(error_t, F&& error)
      : state{state_e::has_error}, error_or_value{error_v, std::forward<F>(error)} {}

  template <typename U>
  explicit constexpr storage_type(value_t, U&& value)
      : state{state_e::has_value}, error_or_value{value_v, std::forward<U>(value)} {}

  template <storage_class_e other_storage_class, typename F, typename U>
  explicit constexpr storage_type(storage_type<other_storage_class, F, U>&& other)
      : state{other.state},
        error_or_value{other.state == state_e::empty
                           ? value_type{}
                           : other.state == state_e::has_error
                                 ? value_type{error_v, std::move(other.error_or_value.error)}
                                 : value_type{value_v, std::move(other.error_or_value.value)}} {}

  state_e state{state_e::empty};
  value_type error_or_value;
};

template <storage_class_e storage_class, typename E>
struct storage_type<storage_class, E> {
  using value_type = error_or_value_type<E, empty_type>;

  constexpr storage_type() = default;

  constexpr storage_type(const storage_type&) = default;
  constexpr storage_type& operator=(const storage_type&) = default;
  constexpr storage_type(storage_type&&) = default;
  constexpr storage_type& operator=(storage_type&&) = default;

  constexpr void destroy() {}

  constexpr void reset() { state = state_e::empty; }

  ~storage_type() = default;

  explicit constexpr storage_type(empty_t) {}

  explicit constexpr storage_type(value_t)
      : state{state_e::has_value}, error_or_value{value_v, empty_type{}} {}

  template <typename F>
  constexpr storage_type(error_t, F&& error)
      : state{state_e::has_error}, error_or_value{error_v, std::forward<F>(error)} {}

  template <storage_class_e other_storage_class, typename F>
  explicit constexpr storage_type(storage_type<other_storage_class, F>&& other)
      : state{other.state},
        error_or_value{other.state == state_e::empty
                           ? value_type{}
                           : other.state == state_e::has_error
                                 ? value_type{error_v, std::move(other.error_or_value.error)}
                                 : value_type{value_v, std::move(other.error_or_value.value)}} {}

  state_e state{state_e::empty};
  value_type error_or_value;
};
template <typename E>
struct storage_type<storage_class_e::non_trivial, E> {
  using value_type = error_or_value_type<E, empty_type>;

  constexpr storage_type() = default;

  constexpr storage_type(const storage_type&) = default;
  constexpr storage_type& operator=(const storage_type&) = default;
  constexpr storage_type(storage_type&&) = default;
  constexpr storage_type& operator=(storage_type&&) = default;

  constexpr void destroy() {
    if (state == state_e::has_value) {
      error_or_value.destroy(value_v);
    } else if (state == state_e::has_error) {
      error_or_value.destroy(error_v);
    }
  }

  constexpr void reset() {
    destroy();
    state = state_e::empty;
  }

  ~storage_type() { destroy(); }

  explicit constexpr storage_type(empty_t) {}

  explicit constexpr storage_type(value_t)
      : state{state_e::has_value}, error_or_value{value_v, empty_type{}} {}

  template <typename F>
  constexpr storage_type(error_t, F&& error)
      : state{state_e::has_error}, error_or_value{error_v, std::forward<F>(error)} {}

  template <storage_class_e other_storage_class, typename F>
  explicit constexpr storage_type(storage_type<other_storage_class, F>&& other)
      : state{other.state},
        error_or_value{other.state == state_e::empty
                           ? value_type{}
                           : other.state == state_e::has_error
                                 ? value_type{error_v, std::move(other.error_or_value.error)}
                                 : value_type{value_v, std::move(other.error_or_value.value)}} {}

  state_e state{state_e::empty};
  value_type error_or_value;
};
// Simplified alias of storage_type.
template <typename E, typename... Ts>
using storage = storage_type<storage_class_trait<E, Ts...>, E, Ts...>;

}  // namespace internal
}  // namespace fitx

#endif  // LIB_FITX_INTERNAL_RESULT_H_
