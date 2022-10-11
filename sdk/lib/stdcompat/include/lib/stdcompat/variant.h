// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VARIANT_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VARIANT_H_

#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>

#include "internal/exception.h"
#include "internal/variant.h"
#include "utility.h"
#include "version.h"

#if defined(__cpp_lib_variant) && __cpp_lib_variant >= 2016L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#include <variant>

namespace cpp17 {

using std::bad_variant_access;
using std::get;
using std::holds_alternative;
using std::monostate;
using std::variant;
using std::variant_alternative;
using std::variant_alternative_t;
using std::variant_size;
using std::variant_size_v;
using std::visit;

}  // namespace cpp17

#else  // Provide polyfill for std::variant and related constructs.

#include <exception>
#include <new>

#include "internal/constructors.h"
#include "internal/storage.h"
#include "internal/utility.h"
#include "type_traits.h"

namespace cpp17 {

// A default-constructible type that may be used as the first variant type to
// make cpp17::variant default-constructible when other variants are not. This
// type may also be used as an alternative representing an empty value.
struct monostate final {
  constexpr bool operator==(const monostate& other) const { return true; }
  constexpr bool operator!=(const monostate& other) const { return false; }
  constexpr bool operator<(const monostate& other) const { return false; }
  constexpr bool operator>(const monostate& other) const { return false; }
  constexpr bool operator<=(const monostate& other) const { return true; }
  constexpr bool operator>=(const monostate& other) const { return true; }
};

// Forward declaration.
template <typename... Ts>
class variant;

// Gets the type of a variant alternative with the given index.
template <std::size_t Index, typename Variant>
struct variant_alternative;

template <std::size_t Index, typename... Ts>
struct variant_alternative<Index, variant<Ts...>>
    : ::cpp17::internal::variant_alternative<Index, ::cpp17::internal::variant_list<Ts...>> {};

template <std::size_t index, typename Variant>
using variant_alternative_t = typename variant_alternative<index, Variant>::type;

// Gets the number of alternatives in a variant as a compile-time constant
// expression.
template <typename T>
struct variant_size;

template <typename... Ts>
struct variant_size<variant<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template <typename T>
struct variant_size<const T> : variant_size<T> {};
template <typename T>
struct variant_size<volatile T> : variant_size<T> {};
template <typename T>
struct variant_size<const volatile T> : variant_size<T> {};

#if defined(__cpp_inline_variables) && __cpp_inline_variables >= 201606L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <typename T>
inline constexpr std::size_t variant_size_v = variant_size<T>::value;

inline constexpr std::size_t variant_npos = internal::empty_index;

#else  // Provide storage for static class variable.

template <typename T>
static constexpr const std::size_t& variant_size_v =
    internal::inline_storage<T, std::size_t, variant_size<T>::value>::storage;

namespace internal {
// Unique type for providing static storage for variant npos.
struct variant_npos_storage {};
}  // namespace internal

static constexpr auto& variant_npos =
    internal::inline_storage<internal::variant_npos_storage, std::size_t,
                             internal::empty_index>::storage;

#endif  // __cpp_inline_variables >= 201606L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

// Exception type to report bad accesses to variant.
class bad_variant_access : public std::exception {
 public:
  bad_variant_access() noexcept {}

  const char* what() const noexcept override { return reason_; }

 private:
  template <typename... Ts>
  friend class variant;

  template <typename T,
            typename std::enable_if<std::is_base_of<std::exception, T>::value, bool>::type>
  friend constexpr void cpp17::internal::throw_or_abort(const char*);

  bad_variant_access(const char* reason) noexcept : reason_{reason} {}

  // String describing the reason for the bad access. Must point to a string
  // with static storage duration.
  const char* reason_;
};

namespace internal {

// Helper type to check that conversions do not narrow.
template <typename T>
struct check_narrow {
  T x[1];
};

// Builds a check(Ti) function for each alternative Ti. This trait is evaluated
// for each element of cpp17::variant<Ts...>. Essentially: for (Index, Ti) in Ts.
//
// Index is the zero-based index of the corresponding element Ti in the pack Ts.
// T is the type deduced from the converting constructor or assignment operator
// of cpp17::variant for which we want to find an appropriately convertible
// element.
//
// The specializations below match the element Ti that passes the conversion
// checks.
template <std::size_t Index, typename T, typename Ti,
          bool IsBool = std::is_same<bool, std::remove_cv_t<Ti>>::value, typename = void>
struct check_valid_option {
  // Non-static so that check_valid_option<...>::check is always a valid
  // name, but doesn't participate in the overload resolution in the
  // valid_option_index selection trait.
  void check();
};

// Checks that Ti x[] = {std::forward<T>()} is well formed.
template <std::size_t Index, typename T, typename Ti>
struct check_valid_option<Index, T, Ti, false,
                          void_t<decltype(check_narrow<Ti>{{std::declval<T>()}})>> {
  static std::integral_constant<std::size_t, Index> check(Ti);
};

// Checks that remove_cvref_t<T> is bool when Ti is cv bool.
template <std::size_t Index, typename T, typename Ti>
struct check_valid_option<Index, T, Ti, true,
                          std::enable_if_t<std::is_same<bool, ::cpp20::remove_cvref_t<T>>::value>> {
  static std::integral_constant<std::size_t, Index> check(Ti);
};

// Mixes in instantiations of check_valid_option for each element in
// cpp17::variant<Ts...>, creating a set of check(Ti) functions that might match
// T following the conversion rules.
template <typename T, typename VariantList,
          typename = std::make_index_sequence<variant_list_size<VariantList>::value>>
struct find_valid_option {
  // Non-static so that find_valid_option<...>::check is always a valid name
  // in the using clause of the recursive case, but doesn't participate in the
  // overload resolution in the valid_option_index trait.
  void check();
};

// Recursive case. This would be simpler with C++17 pack expansion in using
// directives, but this must function in C++14.
template <typename T, typename Ti, std::size_t Index, typename... Ts, std::size_t... Is>
struct find_valid_option<T, variant_list<Ti, Ts...>, std::index_sequence<Index, Is...>>
    : check_valid_option<Index, T, Ti>,
      find_valid_option<T, variant_list<Ts...>, std::index_sequence<Is...>> {
  // Introduce the base class definitions of check() into this scope. The
  // static check(Ti) methods participate in overload resolution in the
  // valid_option_index trait, while the non-static check() methods are
  // ignored.
  using check_valid_option<Index, T, Ti>::check;
  using find_valid_option<T, variant_list<Ts...>, std::index_sequence<Is...>>::check;
};

// Evaluates to the index of the valid target type Ti selected from
// cpp17::variant<Ts...>. The type expression is well formed IFF a single valid
// target type is available that converts from T.
template <typename T, typename VariantList>
using valid_option_index = decltype(find_valid_option<T, VariantList>::check(std::declval<T>()));

// Evaluates to the index of the valid target Ti that converts from T or the
// reserved empty index when no uniquely suitable option is available.
template <typename T, typename Variant, typename = void>
struct selected_index : std::integral_constant<std::size_t, ::cpp17::internal::empty_index> {};

template <typename T, typename... Ts>
struct selected_index<T, variant<Ts...>, void_t<valid_option_index<T, variant_list<Ts...>>>>
    : valid_option_index<T, variant_list<Ts...>> {};

}  // namespace internal

// A resonably complete implementation of std::variant compatible with C++14.
template <typename... Ts>
class variant
    : private ::cpp17::internal::modulate_default_constructor<::cpp17::internal::first_t<Ts...>>,
      private ::cpp17::internal::modulate_copy_and_move<Ts...> {
 private:
  static_assert(sizeof...(Ts) > 0, "Variant must have at least one type!");

  static constexpr bool nothrow_default_constructible =
      std::is_nothrow_default_constructible<::cpp17::internal::first_t<Ts...>>::value;

  static constexpr bool nothrow_move_constructible =
      conjunction_v<std::is_nothrow_move_constructible<Ts>...>;

  static constexpr auto default_init_v = ::cpp17::internal::default_init_v;
  static constexpr auto trivial_init_v = ::cpp17::internal::trivial_init_v;

  template <typename T>
  using type_tag = ::cpp17::internal::type_tag<T>;
  template <std::size_t Index>
  using index_tag = ::cpp17::internal::index_tag<Index>;

  template <typename T>
  using not_self_type = ::cpp17::internal::not_same_type<variant, T>;

  template <typename T>
  using not_in_place = ::cpp17::internal::not_same_type<in_place_t, T>;

  template <typename T>
  struct occurs_once
      : std::integral_constant<bool, ::cpp17::internal::occurences_of_v<T, Ts...> == 1> {};

  template <typename... Conditions>
  using requires_conditions = ::cpp17::internal::requires_conditions<Conditions...>;

  template <typename... Conditions>
  using assignment_requires_conditions =
      ::cpp17::internal::assignment_requires_conditions<variant&, Conditions...>;

  template <typename T, typename... Args>
  using emplace_constructible_by_type =
      std::enable_if_t<(::cpp17::internal::occurences_of_v<T, Ts...> == 1 &&
                        std::is_constructible<T, Args...>::value),
                       std::add_lvalue_reference_t<T>>;

  template <std::size_t Index, typename = std::enable_if_t<(Index < sizeof...(Ts))>>
  using alternative_t = variant_alternative_t<Index, variant>;

  template <std::size_t Index, typename... Args>
  using emplace_constructible_by_index =
      std::enable_if_t<std::is_constructible<alternative_t<Index>, Args...>::value,
                       std::add_lvalue_reference_t<alternative_t<Index>>>;

  template <typename T>
  static constexpr std::size_t selected_index =
      ::cpp17::internal::selected_index<T, variant>::value;

  template <typename T, typename = std::enable_if<not_self_type<T>::value>>
  using selected_t = alternative_t<selected_index<T>>;

 public:
  // Default constructors.

  constexpr variant() noexcept(nothrow_default_constructible) : storage_{default_init_v} {}

  // Copy/move constructors and assignment operators.

  constexpr variant(const variant&) = default;
  constexpr variant& operator=(const variant&) = default;

  constexpr variant(variant&&) noexcept(nothrow_move_constructible) = default;
  constexpr variant& operator=(variant&&) = default;

  // Converting constructors.

  template <typename T,
            requires_conditions<std::integral_constant<bool, (sizeof...(Ts) > 0)>,
                                not_in_place<T>> = true,
            typename Ti = selected_t<T&&>,
            requires_conditions<occurs_once<Ti>, std::is_constructible<Ti, T>> = true>
  constexpr variant(T&& value) noexcept(std::is_nothrow_constructible<Ti, T>::value)
      : storage_(type_tag<Ti>{}, std::forward<T>(value)) {}

  template <typename T, typename... Args,
            requires_conditions<occurs_once<T>, std::is_constructible<T, Args...>> = true>
  explicit constexpr variant(in_place_type_t<T>, Args&&... args)
      : storage_(type_tag<T>{}, std::forward<Args>(args)...) {}

  template <typename T, typename U, typename... Args,
            requires_conditions<occurs_once<T>, std::is_constructible<T, std::initializer_list<T>&,
                                                                      Args...>> = true>
  explicit constexpr variant(in_place_type_t<T>, std::initializer_list<U> init_list, Args&&... args)
      : storage_(type_tag<T>{}, init_list, std::forward<Args>(args)...) {}

  template <std::size_t Index, typename... Args,
            requires_conditions<std::is_constructible<alternative_t<Index>, Args...>> = true>
  explicit constexpr variant(in_place_index_t<Index>, Args&&... args)
      : storage_(index_tag<Index>{}, std::forward<Args>(args)...) {}

  template <std::size_t Index, typename U, typename... Args,
            requires_conditions<std::is_constructible<alternative_t<Index>,
                                                      std::initializer_list<U>&, Args...>> = true>
  explicit constexpr variant(in_place_index_t<Index>, std::initializer_list<U> init_list,
                             Args&&... args)
      : storage_(index_tag<Index>{}, init_list, std::forward<Args>(args)...) {}

  ~variant() = default;

  // Converting assignment.

  template <typename T>
  constexpr assignment_requires_conditions<
      occurs_once<selected_t<T>>, std::is_constructible<selected_t<T&&>, T>,
      std::is_assignable<selected_t<T&&>&, T>,
      disjunction<std::is_nothrow_constructible<selected_t<T&&>, T>,
                  negation<std::is_nothrow_move_constructible<selected_t<T&&>>>>>
  operator=(T&& value) noexcept(std::is_nothrow_assignable<selected_t<T&&>&, T>::value&&
                                    std::is_nothrow_constructible<selected_t<T&&>, T>::value) {
    constexpr auto index = selected_index<T>;
    if (storage_.index() == index) {
      storage_.get(index_tag<index>{}) = std::forward<T>(value);
    } else {
      this->emplace<index>(std::forward<T>(value));
    }
    return *this;
  }

  template <typename T>
  constexpr assignment_requires_conditions<
      occurs_once<selected_t<T>>, std::is_constructible<selected_t<T&&>, T>,
      std::is_assignable<selected_t<T&&>&, T>,
      conjunction<negation<std::is_nothrow_constructible<selected_t<T&&>, T>>,
                  std::is_nothrow_move_constructible<selected_t<T&&>>>>
  operator=(T&& value) noexcept(std::is_nothrow_assignable<selected_t<T&&>&, T>::value&&
                                    std::is_nothrow_constructible<selected_t<T&&>, T>::value) {
    constexpr auto index = selected_index<T>;
    if (storage_.index() == index) {
      storage_.get(index_tag<index>{}) = std::forward<T>(value);
    } else {
      this->operator=(variant(std::forward<T>(value)));
    }
    return *this;
  }

  constexpr std::size_t index() const noexcept { return storage_.index(); }

  // Emplacement.

  template <typename T, typename... Args>
  constexpr emplace_constructible_by_type<T, Args&&...> emplace(Args&&... args) {
    storage_.reset();
    storage_.construct(type_tag<T>{}, std::forward<Args>(args)...);
    return storage_.get(type_tag<T>{});
  }

  template <typename T, typename U, typename... Args>
  constexpr emplace_constructible_by_type<T, std::initializer_list<U>&, Args&&...> emplace(
      std::initializer_list<U> init_list, Args&&... args) {
    storage_.reset();
    storage_.construct(type_tag<T>{}, init_list, std::forward<Args>(args)...);
    return storage_.get(type_tag<T>{});
  }

  template <std::size_t Index, typename... Args>
  constexpr emplace_constructible_by_index<Index, Args&&...> emplace(Args&&... args) {
    storage_.reset();
    storage_.construct(index_tag<Index>{}, std::forward<Args>(args)...);
    return storage_.get(index_tag<Index>{});
  }

  template <std::size_t Index, typename U, typename... Args>
  constexpr emplace_constructible_by_index<Index, std::initializer_list<U>&, Args&&...> emplace(
      std::initializer_list<U> init_list, Args&&... args) {
    storage_.reset();
    storage_.construct(index_tag<Index>{}, init_list, std::forward<Args>(args)...);
    return storage_.get(index_tag<Index>{});
  }

  // Swap.

  void swap(variant& other) {
    other.storage_ = cpp20::exchange(storage_, std::move(other.storage_));
  }

  // Comparison.

  friend constexpr bool operator==(const variant& lhs, const variant& rhs) {
    bool result = false;
    const bool has_value =
        lhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (lhs.index() != rhs.index()) {
            result = false;
          } else {
            result = lhs.storage_.get(active_index_tag) == rhs.storage_.get(active_index_tag);
          }
        });
    return !has_value || result;
  }
  friend constexpr bool operator!=(const variant& lhs, const variant& rhs) {
    bool result = true;
    const bool has_value =
        lhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (lhs.index() != rhs.index()) {
            result = true;
          } else {
            result = lhs.storage_.get(active_index_tag) != rhs.storage_.get(active_index_tag);
          }
        });
    return has_value && result;
  }
  friend constexpr bool operator<(const variant& lhs, const variant& rhs) {
    bool result = true;
    const bool has_value =
        rhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (lhs.storage_.is_empty()) {
            result = true;
          } else if (lhs.index() < rhs.index()) {
            result = true;
          } else if (lhs.index() > rhs.index()) {
            result = false;
          } else {
            result = lhs.storage_.get(active_index_tag) < rhs.storage_.get(active_index_tag);
          }
        });
    return has_value && result;
  }
  friend constexpr bool operator>(const variant& lhs, const variant& rhs) {
    bool result = true;
    const bool has_value =
        lhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (rhs.storage_.is_empty()) {
            result = true;
          } else if (lhs.index() > rhs.index()) {
            result = true;
          } else if (lhs.index() < rhs.index()) {
            result = false;
          } else {
            result = lhs.storage_.get(active_index_tag) > rhs.storage_.get(active_index_tag);
          }
        });
    return has_value && result;
  }
  friend constexpr bool operator<=(const variant& lhs, const variant& rhs) {
    bool result = false;
    const bool has_value =
        lhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (rhs.storage_.is_empty()) {
            result = false;
          } else if (lhs.index() < rhs.index()) {
            result = true;
          } else if (lhs.index() > rhs.index()) {
            result = false;
          } else {
            result = lhs.storage_.get(active_index_tag) <= rhs.storage_.get(active_index_tag);
          }
        });
    return !has_value || result;
  }
  friend constexpr bool operator>=(const variant& lhs, const variant& rhs) {
    bool result = false;
    const bool has_value =
        rhs.storage_.visit([&result, &lhs, &rhs](auto, auto active_index_tag, const auto*) {
          if (lhs.storage_.is_empty()) {
            result = false;
          } else if (lhs.index() > rhs.index()) {
            result = true;
          } else if (lhs.index() < rhs.index()) {
            result = false;
          } else {
            result = lhs.storage_.get(active_index_tag) >= rhs.storage_.get(active_index_tag);
          }
        });
    return !has_value || result;
  }

  constexpr bool valueless_by_exception() const { return storage_.is_empty(); }

 private:
  [[noreturn]] static constexpr void exception_invalid_index() {
    internal::throw_or_abort<bad_variant_access>("Invalid variant index for cpp17::get<>");
  }

  [[noreturn]] static constexpr void exception_invalid_type() {
    internal::throw_or_abort<bad_variant_access>("Invalid variant type for cpp17::get<>");
  }

  ::cpp17::internal::storage_type<Ts...> storage_;

  // Friend for cpp17::get.
  template <std::size_t Index, typename... Args>
  friend constexpr cpp17::variant_alternative_t<Index, variant<Args...>>& get(
      variant<Args...>& value);

  template <std::size_t Index, typename... Args>
  friend constexpr const cpp17::variant_alternative_t<Index, variant<Args...>>& get(
      const variant<Args...>& value);

  template <std::size_t Index, typename... Args>
  friend constexpr cpp17::variant_alternative_t<Index, variant<Args...>>&& get(
      variant<Args...>&& value);

  template <std::size_t Index, typename... Args>
  friend constexpr const cpp17::variant_alternative_t<Index, variant<Args...>>&& get(
      const variant<Args...>&& value);

  template <typename T, typename... Args>
  friend constexpr T& get(variant<Args...>& value);

  template <typename T, typename... Args>
  friend constexpr const T& get(const variant<Args...>& value);

  template <typename T, typename... Args>
  friend constexpr T&& get(variant<Args...>&& value);

  template <typename T, typename... Args>
  friend constexpr const T&& get(const variant<Args...>&& value);
};

// Swaps variants.
template <typename... Ts,
          ::cpp17::internal::requires_conditions<std::is_move_constructible<Ts>...,
                                                 ::cpp17::internal::is_swappable<Ts>...> = true>
void swap(variant<Ts...>& a, variant<Ts...>& b) {
  a.swap(b);
}

// Accesses the variant by zero-based index.
template <std::size_t Index, typename... Ts>
constexpr cpp17::variant_alternative_t<Index, variant<Ts...>>& get(variant<Ts...>& value) {
  if (value.storage_.has_value(::cpp17::internal::index_tag<Index>{})) {
    return value.storage_.get(::cpp17::internal::index_tag<Index>{});
  }
  value.exception_invalid_index();
}

template <std::size_t Index, typename... Ts>
constexpr cpp17::variant_alternative_t<Index, variant<Ts...>>&& get(variant<Ts...>&& value) {
  if (value.storage_.has_value(::cpp17::internal::index_tag<Index>{})) {
    return value.storage_.get(::cpp17::internal::index_tag<Index>{});
  }
  value.exception_invalid_index();
}
template <std::size_t Index, typename... Ts>
constexpr const cpp17::variant_alternative_t<Index, variant<Ts...>>& get(
    const variant<Ts...>& value) {
  if (value.storage_.has_value(::cpp17::internal::index_tag<Index>{})) {
    return value.storage_.get(::cpp17::internal::index_tag<Index>{});
  }
  value.exception_invalid_index();
}
template <std::size_t Index, typename... Ts>
constexpr const cpp17::variant_alternative_t<Index, variant<Ts...>>&& get(
    const variant<Ts...>&& value) {
  if (value.storage_.has_value(::cpp17::internal::index_tag<Index>{})) {
    return value.storage_.get(::cpp17::internal::index_tag<Index>{});
  }
  value.exception_invalid_index();
}

// Accesses the variant by unique type. See note above about ADL.
template <typename T, typename... Ts>
constexpr T& get(variant<Ts...>& value) {
  if (value.storage_.has_value(::cpp17::internal::type_tag<T>{})) {
    return value.storage_.get(::cpp17::internal::type_tag<T>{});
  }
  value.exception_invalid_type();
}
template <typename T, typename... Ts>
constexpr T&& get(variant<Ts...>&& value) {
  if (value.storage_.has_value(::cpp17::internal::type_tag<T>{})) {
    return value.storage_.get(::cpp17::internal::type_tag<T>{});
  }
  value.exception_invalid_type();
}
template <typename T, typename... Ts>
constexpr const T& get(const variant<Ts...>& value) {
  if (value.storage_.has_value(::cpp17::internal::type_tag<T>{})) {
    return value.storage_.get(::cpp17::internal::type_tag<T>{});
  }
  value.exception_invalid_type();
}
template <typename T, typename... Ts>
constexpr const T&& get(const variant<Ts...>&& value) {
  if (value.storage_.has_value(::cpp17::internal::type_tag<T>{})) {
    return value.storage_.get(::cpp17::internal::type_tag<T>{});
  }
  value.exception_invalid_type();
}

// Checks if the variant holds type T. See note above about ADL.
template <typename T, typename... Ts>
constexpr bool holds_alternative(const variant<Ts...>& value) {
  constexpr auto index = ::cpp17::internal::selected_index<T, variant<Ts...>>::value;
  return value.index() == index;
}

namespace internal {

struct dispatcher_container {
  // Creates a compile dispatcher that calls |visitor| with the right set of
  // active alternatives each variant.
  template <std::size_t... Indexes>
  struct dispatcher {
    template <typename Visitor, typename... Variants>
    static constexpr decltype(auto) dispatch(Visitor visitor, Variants... variants) {
      return cpp20::invoke(visitor, cpp17::get<Indexes>(variants)...);
    }
  };
};

}  // namespace internal

template <typename Visitor, typename... Variants>
constexpr decltype(auto) visit(Visitor&& visitor, Variants&&... variants) {
#if defined(__cpp_exceptions) && __cpp_exceptions >= 199711L
  internal::throw_or_abort_if_any<bad_variant_access>(
      "cpp17::visit encountered a valuless_by__exception variant. ",
      variants.valueless_by_exception()...);
#endif
  using matrix = internal::visit_matrix<Visitor, Variants...>;
  constexpr auto mat = matrix::template make_dispatcher_array<
      internal::dispatcher_container,
      std::make_index_sequence<cpp17::variant_size_v<cpp20::remove_cvref_t<Variants>>>...>();
  return matrix::at(mat, variants.index()...)(std::forward<Visitor>(visitor),
                                              std::forward<Variants>(variants)...);
}

}  // namespace cpp17

#endif  // __cpp_lib_variant >= 2016L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_VARIANT_H_
