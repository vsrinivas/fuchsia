// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_VARIANT_H_
#define LIB_FIT_VARIANT_H_

#include <exception>
#include <new>
#include <type_traits>
#include <utility>

#include "constructors_internal.h"
#include "in_place_internal.h"
#include "storage_internal.h"
#include "traits.h"
#include "utility_internal.h"

namespace fit {

// A default-constructible type that may be used as the first variant type to
// make fit::variant default-constructible when other variants are not. This
// type may also be used as an alternative representing an empty value.
struct monostate final {
  constexpr bool operator==(const monostate& other) const { return true; }
  constexpr bool operator!=(const monostate& other) const { return false; }
  constexpr bool operator<(const monostate& other) const { return false; }
  constexpr bool operator>(const monostate& other) const { return false; }
  constexpr bool operator<=(const monostate& other) const { return true; }
  constexpr bool operator>=(const monostate& other) const { return true; }
};

namespace internal {

// Helper type to avoid recursive instantiations of the full variant type.
template <typename...>
struct variant_list {};

// Gets the number of alternatives in a variant_list as a compile-time constant.
template <typename T>
struct variant_list_size;

template <typename... Ts>
struct variant_list_size<variant_list<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)> {};

// Helper to get the type of a variant_list alternative with the given index.
template <size_t Index, typename VariantList>
struct variant_alternative;

template <size_t Index, typename T0, typename... Ts>
struct variant_alternative<Index, variant_list<T0, Ts...>>
    : variant_alternative<Index - 1, variant_list<Ts...>> {};

template <typename T0, typename... Ts>
struct variant_alternative<0, variant_list<T0, Ts...>> {
  using type = T0;
};

}  // namespace internal

// Forward declaration.
template <typename... Ts>
class variant;

// Gets the type of a variant alternative with the given index.
template <size_t Index, typename Variant>
struct variant_alternative;

template <size_t Index, typename... Ts>
struct variant_alternative<Index, variant<Ts...>>
    : ::fit::internal::variant_alternative<Index, ::fit::internal::variant_list<Ts...>> {};

template <size_t index, typename Variant>
using variant_alternative_t = typename variant_alternative<index, Variant>::type;

// Gets the number of alternatives in a variant as a compile-time constant
// expression.
template <typename T>
struct variant_size;

template <typename... Ts>
struct variant_size<variant<Ts...>> : std::integral_constant<size_t, sizeof...(Ts)> {};

template <typename T>
struct variant_size<const T> : variant_size<T> {};
template <typename T>
struct variant_size<volatile T> : variant_size<T> {};
template <typename T>
struct variant_size<const volatile T> : variant_size<T> {};

#ifdef __cpp_inline_variables

template <typename T>
inline constexpr size_t variant_size_v = variant_size<T>::value;

#else

template <typename T>
struct variant_size_holder {
  static constexpr size_t value{variant_size<T>::value};
};

template <typename T>
constexpr size_t variant_size_holder<T>::value;

template <typename T>
static constexpr const size_t& variant_size_v = variant_size_holder<T>::value;

#endif

// Exception type to report bad accesses to variant.
class bad_variant_access : public std::exception {
 public:
  bad_variant_access() noexcept {}

  const char* what() const noexcept override { return reason_; }

 private:
  template <typename... Ts>
  friend class variant;

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
// for each element of fit::variant<Ts...>. Essentially: for (Index, Ti) in Ts.
//
// Index is the zero-based index of the corresponding element Ti in the pack Ts.
// T is the type deduced from the converting constructor or assignment operator
// of fit::variant for which we want to find an appropriately convertible
// element.
//
// The specializations below match the element Ti that passes the conversion
// checks.
template <size_t Index, typename T, typename Ti,
          bool IsBool = std::is_same<bool, std::remove_cv_t<Ti>>::value, typename = void>
struct check_valid_option {
  // Non-static so that check_valid_option<...>::check is always a valid
  // name, but doesn't participate in the overload resolution in the
  // valid_option_index selection trait.
  void check();
};

// Checks that Ti x[] = {std::forward<T>()} is well formed.
template <size_t Index, typename T, typename Ti>
struct check_valid_option<Index, T, Ti, false,
                          void_t<decltype(check_narrow<Ti>{{std::declval<T>()}})>> {
  static std::integral_constant<size_t, Index> check(Ti);
};

// Checks that remove_cvref_t<T> is bool when Ti is cv bool.
template <size_t Index, typename T, typename Ti>
struct check_valid_option<Index, T, Ti, true,
                          std::enable_if_t<std::is_same<bool, remove_cvref_t<T>>::value>> {
  static std::integral_constant<size_t, Index> check(Ti);
};

// Mixes in instantiations of check_valid_option for each element in
// fit::variant<Ts...>, creating a set of check(Ti) functions that might match
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
template <typename T, typename Ti, size_t Index, typename... Ts, size_t... Is>
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
// fit::variant<Ts...>. The type expression is well formed IFF a single valid
// target type is available that converts from T.
template <typename T, typename VariantList>
using valid_option_index = decltype(find_valid_option<T, VariantList>::check(std::declval<T>()));

// Evaluates to the index of the valid target Ti that converts from T or the
// reserved empty index when no uniquely suitable option is available.
template <typename T, typename Variant, typename = void>
struct selected_index : std::integral_constant<size_t, ::fit::internal::empty_index> {};

template <typename T, typename... Ts>
struct selected_index<T, variant<Ts...>, void_t<valid_option_index<T, variant_list<Ts...>>>>
    : valid_option_index<T, variant_list<Ts...>> {};

}  // namespace internal

// A resonably complete implementation of std::variant compatible with C++14.
template <typename... Ts>
class variant
    : private ::fit::internal::modulate_default_constructor<::fit::internal::first_t<Ts...>>,
      private ::fit::internal::modulate_copy_and_move<Ts...> {
 private:
  static_assert(sizeof...(Ts) > 0, "Variant must have at least one type!");

  static constexpr bool nothrow_default_constructible =
      std::is_nothrow_default_constructible<::fit::internal::first_t<Ts...>>::value;

  static constexpr bool nothrow_move_constructible =
      conjunction_v<std::is_nothrow_move_constructible<Ts>...>;

  static constexpr auto default_init_v = ::fit::internal::default_init_v;
  static constexpr auto trivial_init_v = ::fit::internal::trivial_init_v;

  template <typename T>
  using type_tag = ::fit::internal::type_tag<T>;
  template <size_t Index>
  using index_tag = ::fit::internal::index_tag<Index>;

  template <typename T>
  using not_self_type = ::fit::internal::not_same_type<variant, T>;

  template <typename T>
  using not_in_place = ::fit::internal::not_same_type<in_place_t, T>;

  template <typename T>
  struct occurs_once
      : std::integral_constant<bool, ::fit::internal::occurences_of_v<T, Ts...> == 1> {};

  template <typename... Conditions>
  using requires_conditions = ::fit::internal::requires_conditions<Conditions...>;

  template <typename... Conditions>
  using assignment_requires_conditions =
      ::fit::internal::assignment_requires_conditions<variant&, Conditions...>;

  template <typename T, typename... Args>
  using emplace_constructible_by_type =
      std::enable_if_t<(::fit::internal::occurences_of_v<T, Ts...> == 1 &&
                        std::is_constructible<T, Args...>::value),
                       std::add_lvalue_reference_t<T>>;

  template <size_t Index, typename = std::enable_if_t<(Index < sizeof...(Ts))>>
  using alternative_t = variant_alternative_t<Index, variant>;

  template <size_t Index, typename... Args>
  using emplace_constructible_by_index =
      std::enable_if_t<std::is_constructible<alternative_t<Index>, Args...>::value,
                       std::add_lvalue_reference_t<alternative_t<Index>>>;

  template <typename T>
  static constexpr size_t selected_index = ::fit::internal::selected_index<T, variant>::value;

  template <typename T, typename = std::enable_if<not_self_type<T>::value>>
  using selected_t = alternative_t<selected_index<T>>;

  [[noreturn]] static constexpr void throw_bad_variant_access(const char* reason) {
#if __cpp_exceptions
    throw bad_variant_access(reason);
#else
    (void)reason;
    __builtin_abort();
#endif
  }

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

  template <size_t Index, typename... Args,
            requires_conditions<std::is_constructible<alternative_t<Index>, Args...>> = true>
  explicit constexpr variant(in_place_index_t<Index>, Args&&... args)
      : storage_(index_tag<Index>{}, std::forward<Args>(args)...) {}

  template <size_t Index, typename U, typename... Args,
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

  constexpr size_t index() const noexcept { return storage_.index(); }

  // TODO(eieio): Remove uses of these in favor of non-member get.
  template <size_t Index>
  constexpr auto& get() & {
    if (storage_.has_value(index_tag<Index>{})) {
      return storage_.get(index_tag<Index>{});
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <size_t Index>
  constexpr const auto& get() const& {
    if (storage_.has_value(index_tag<Index>{})) {
      return storage_.get(index_tag<Index>{});
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <size_t Index>
  constexpr auto&& get() && {
    if (storage_.has_value(index_tag<Index>{})) {
      return std::move(storage_.get(index_tag<Index>{}));
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <size_t Index>
  constexpr const auto&& get() const&& {
    if (storage_.has_value(index_tag<Index>{})) {
      return std::move(storage_.get(index_tag<Index>{}));
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }

  template <typename T>
  constexpr auto& get() & {
    if (storage_.has_value(type_tag<T>{})) {
      return storage_.get(type_tag<T>{});
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <typename T>
  constexpr const auto& get() const& {
    if (storage_.has_value(type_tag<T>{})) {
      return storage_.get(type_tag<T>{});
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <typename T>
  constexpr auto&& get() && {
    if (storage_.has_value(type_tag<T>{})) {
      return std::move(storage_.get(type_tag<T>{}));
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }
  template <typename T>
  constexpr const auto&& get() const&& {
    if (storage_.has_value(type_tag<T>{})) {
      return std::move(storage_.get(type_tag<T>{}));
    } else {
      throw_bad_variant_access("Bad get<>() from variant!");
    }
  }

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

  template <size_t Index, typename... Args>
  constexpr emplace_constructible_by_index<Index, Args&&...> emplace(Args&&... args) {
    storage_.reset();
    storage_.construct(index_tag<Index>{}, std::forward<Args>(args)...);
    return storage_.get(index_tag<Index>{});
  }

  template <size_t Index, typename U, typename... Args>
  constexpr emplace_constructible_by_index<Index, std::initializer_list<U>&, Args&&...> emplace(
      std::initializer_list<U> init_list, Args&&... args) {
    storage_.reset();
    storage_.construct(index_tag<Index>{}, init_list, std::forward<Args>(args)...);
    return storage_.get(index_tag<Index>{});
  }

  // Swap.

  void swap(variant& other) { storage_.swap(other.storage_); }

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

 private:
  ::fit::internal::storage_type<Ts...> storage_;
};

// Swaps variants.
template <typename... Ts>
void swap(variant<Ts...>& a, variant<Ts...>& b) {
  a.swap(b);
}

// Accesses the variant by zero-based index.
//
// Accesses should use ADL, similar to the pattern for std::swap:
//
//  using std::get;
//  get<some_index>(some_fit_variant);
//
// This makes code adaptable to substituting std::variant for fit::variant on
// newer compilers.
template <size_t Index, typename... Ts>
constexpr auto& get(variant<Ts...>& value) {
  return value.template get<Index>();
}
template <size_t Index, typename... Ts>
constexpr auto&& get(variant<Ts...>&& value) {
  return std::move(value).template get<Index>();
}
template <size_t Index, typename... Ts>
constexpr const auto& get(const variant<Ts...>& value) {
  return value.template get<Index>();
}
template <size_t Index, typename... Ts>
constexpr const auto&& get(const variant<Ts...>&& value) {
  return std::move(value).template get<Index>();
}

// Accesses the variant by unique type. See note above about ADL.
template <typename T, typename... Ts>
constexpr auto& get(variant<Ts...>& value) {
  return value.template get<T>();
}
template <typename T, typename... Ts>
constexpr auto&& get(variant<Ts...>&& value) {
  return std::move(value).template get<T>();
}
template <typename T, typename... Ts>
constexpr const auto& get(const variant<Ts...>& value) {
  return value.template get<T>();
}
template <typename T, typename... Ts>
constexpr const auto&& get(const variant<Ts...>&& value) {
  return std::move(value).template get<T>();
}

// Checks if the variant holds type T. See note above about ADL.
template <typename T, typename... Ts>
constexpr bool holds_alternative(const variant<Ts...>& value) {
  constexpr auto index = ::fit::internal::selected_index<T, variant<Ts...>>::value;
  return value.index() == index;
}

// TODO(eieio): Remove once the old ::fit::internal spellings of these types is
// removed from FIDL.
namespace internal {

using ::fit::monostate;
using ::fit::variant;

}  // namespace internal

}  // namespace fit

#endif  // LIB_FIT_VARIANT_H_
