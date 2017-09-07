// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace fbl {

template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;

    using value_type = T;
    using type = integral_constant<T, v>;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

// is_const:

template <typename T>
struct is_const : false_type {};

template <typename T>
struct is_const<const T> : true_type {};

// is_lvalue_reference:

template <typename T>
struct is_lvalue_reference : false_type {};

template <typename T>
struct is_lvalue_reference<T&> : true_type {};

// is_rvalue_reference:

template <typename T>
struct is_rvalue_reference : false_type {};

template <typename T>
struct is_rvalue_reference<T&&> : true_type {};

// remove_reference:

template <typename T>
struct remove_reference {
    using type = T;
};

template <typename T>
struct remove_reference<T&> {
    using type = T;
};

template <typename T>
struct remove_reference<T&&> {
    using type = T;
};

template <typename T>
constexpr typename remove_reference<T>::type&& move(T&& t) {
    return static_cast<typename remove_reference<T>::type&&>(t);
}

// remove_const:

template <typename T>
struct remove_const {
    typedef T type;
};

template <typename T>
struct remove_const<const T> {
    typedef T type;
};

// remove_volatile:

template <typename T>
struct remove_volatile {
    typedef T type;
};

template <typename T>
struct remove_volatile<volatile T> {
    typedef T type;
};

// remove_cv:

template <typename T>
struct remove_cv {
    typedef typename remove_volatile<typename remove_const<T>::type>::type type;
};

// forward:

template <typename T>
constexpr T&& forward(typename remove_reference<T>::type& t) {
    return static_cast<T&&>(t);
}

template <typename T>
constexpr T&& forward(typename remove_reference<T>::type&& t) {
    static_assert(!is_lvalue_reference<T>::value, "bad util::forward call");
    return static_cast<T&&>(t);
}

// is_same:

template<class T, class U> struct is_same : false_type {};
template<class T> struct is_same<T, T> : true_type {};

// enable_if:

template<bool B, class T = void> struct enable_if { };
template<class T> struct enable_if<true, T> {
    typedef T type;
};

// conditional:

template<bool B, class T, class F>
struct conditional { typedef T type; };

template<class T, class F>
struct conditional<false, T, F> { typedef F type; };

// is_integral.  By default, T is not integral (aka, not an integer)
template <typename T>
struct is_integral : false_type {};

// Specializations.  Every basic integral type needs to be called out.
template <> struct is_integral<bool>                   : true_type {};
template <> struct is_integral<char>                   : true_type {};
template <> struct is_integral<char16_t>               : true_type {};
template <> struct is_integral<char32_t>               : true_type {};
template <> struct is_integral<wchar_t>                : true_type {};
template <> struct is_integral<signed char>            : true_type {};
template <> struct is_integral<unsigned char>          : true_type {};
template <> struct is_integral<short int>              : true_type {};
template <> struct is_integral<unsigned short int>     : true_type {};
template <> struct is_integral<int>                    : true_type {};
template <> struct is_integral<unsigned int>           : true_type {};
template <> struct is_integral<long int>               : true_type {};
template <> struct is_integral<unsigned long int>      : true_type {};
template <> struct is_integral<long long int>          : true_type {};
template <> struct is_integral<unsigned long long int> : true_type {};

// is_floating_point.  By default, T is not a floating point type.
template <typename T>
struct is_floating_point : false_type {};

// Specializations.  Every basic floating point type needs to be called out.
template <> struct is_floating_point<float>       : true_type {};
template <> struct is_floating_point<double>      : true_type {};
template <> struct is_floating_point<long double> : true_type {};

// Arithmetic data types are either floats or integers
template <typename T>
struct is_arithmetic :
    integral_constant<bool, is_integral<T>::value || is_floating_point<T>::value> { };

namespace internal {

template<typename T, bool = is_arithmetic<T>::value>
struct is_signed : integral_constant<bool, T(-1) < T(0)> {};
template<typename T>
struct is_signed<T, false> : fbl::false_type {};

template<typename T, bool = is_arithmetic<T>::value>
struct is_unsigned : integral_constant<bool, T(0) < T(-1)> {};
template<typename T>
struct is_unsigned<T, false> : fbl::false_type {};

template<typename T, bool = is_integral<T>::value>
struct is_signed_integer : integral_constant<bool, T(-1) < T(0)> {};
template<typename T>
struct is_signed_integer<T, false> : fbl::false_type {};

template<typename T, bool = is_integral<T>::value>
struct is_unsigned_integer : integral_constant<bool, T(0) < T(-1)> {};
template<typename T>
struct is_unsigned_integer<T, false> : fbl::false_type {};

} // namespace internal

template <typename T>
struct is_signed : internal::is_signed<T>::type {};

template <typename T>
struct is_unsigned : internal::is_unsigned<T>::type {};

template <typename T>
struct is_signed_integer : internal::is_signed_integer<T>::type {};

template <typename T>
struct is_unsigned_integer : internal::is_unsigned_integer<T>::type {};

// is_enum is a builtin
template<typename T>
struct is_enum : integral_constant<bool, __is_enum(T)> { };

// is_pod is a builtin
template<typename T>
struct is_pod : integral_constant<bool, __is_pod(T)> { };

// is_standard_layout is a builtin
template<typename T>
struct is_standard_layout : integral_constant<bool, __is_standard_layout(T)> { };

// underlying_type is a builtin
template<typename T>
struct underlying_type {
  using type = __underlying_type(T);
};

// match_cv: match_cv<SrcType, DestType>::type is DestType cv-qualified in the same way as SrcType.

// Primary template:
template <typename SrcType, typename DestType>
struct match_cv {
  using type = typename remove_cv<DestType>::type;
};

// Specializations for const/volatile/const volatile:
template <typename SrcType, typename DestType>
struct match_cv<const SrcType, DestType> {
  using type = typename remove_cv<DestType>::type const;
};
template <typename SrcType, typename DestType>
struct match_cv<volatile SrcType, DestType> {
  using type = typename remove_cv<DestType>::type volatile;
};
template <typename SrcType, typename DestType>
struct match_cv<const volatile SrcType, DestType> {
  using type = typename remove_cv<DestType>::type const volatile;
};

// is_class builtin
template <typename T>
struct is_class : public integral_constant<bool, __is_class(T)> { };

// is_base_of builtin
template <typename Base, typename Derived>
struct is_base_of : public integral_constant<bool, __is_base_of(Base, Derived)> { };

// has_virtual_destructor
template <typename T>
struct has_virtual_destructor : public integral_constant<bool, __has_virtual_destructor(T)> { };

// has_trivial_destructor
template <typename T>
struct has_trivial_destructor : public integral_constant<bool, __has_trivial_destructor(T)> { };

// is_pointer
namespace internal {
template <typename T>
struct is_pointer : public false_type { };

template <typename T>
struct is_pointer<T*> : public true_type { };
}  // namespace internal

template <typename T>
struct is_pointer :
    public integral_constant<bool,
                             internal::is_pointer<typename remove_cv<T>::type>::value> { };

// is_convertible_pointer
//
// Note: this is a simplified version of std::is_convertible.  Whereas
// std::is_convertible will check to see if any two types are implicitly
// convertible, is_convertible_pointer will only check to see if two pointer
// types are implicitly convertible.  Additionally, no explicit support or tests
// have been added for function pointers conversion.
template <typename From, typename To>
struct is_convertible_pointer {
private:
    static true_type  test(To);
    static false_type test(...);
    static From       make_from_type();

public:
    static constexpr bool value =
        is_pointer<From>::value &&
        is_pointer<To>::value &&
        decltype(test(make_from_type()))::value;
};

// Macro for defining a trait that checks if a type T has a method with the
// given name. This is not as strong as using is_same to check function
// signatures, but checking this trait first gives a better static_assert
// message than the compiler warnings from is_same if the function doesn't
// exist.
// Note that the resulting trait_name will be in the namespace where the macro
// is used.
//
// Example:
//
// DECLARE_HAS_MEMBER_FN(has_bar, Bar);
// template <typname T>
// class Foo {
//   static_assert(has_bar<T>::value, "Foo classes must implement Bar()!");
//   // TODO: use 'if constexpr' to avoid this next static_assert once c++17
//   lands.
//   static_assert(is_same<decltype(&T::Bar), void (T::*)(int)>::value,
//                 "Bar must be a non-static member function with signature "
//                 "'void Bar(int)', and must be visible to Foo (either "
//                 "because it is public, or due to friendship).");
//  };
#define DECLARE_HAS_MEMBER_FN(trait_name, fn_name)                                \
template <typename T>                                                             \
struct trait_name {                                                               \
private:                                                                          \
    template <typename C> static ::fbl::true_type test( decltype(&C::fn_name) ); \
    template <typename C> static ::fbl::false_type test(...);                    \
                                                                                  \
public:                                                                           \
    static constexpr bool value = decltype(test<T>(nullptr))::value;              \
}

}  // namespace fbl
