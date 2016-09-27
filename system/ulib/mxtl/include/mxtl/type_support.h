// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace mxtl {

template <typename T, T v>
struct integral_constant {
    static constexpr T value = v;

    using value_type = T;
    using type = integral_constant<T, v>;
};

using true_type = integral_constant<bool, true>;
using false_type = integral_constant<bool, false>;

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

template <typename T>
struct is_signed : integral_constant<bool, is_arithmetic<T>::value && (T(-1) < T(0))> { };

template <typename T>
struct is_unsigned : integral_constant<bool, is_arithmetic<T>::value && (T(0) < T(-1))> { };

template <typename T>
struct is_signed_integer : integral_constant<bool, is_integral<T>::value && (T(-1) < T(0))> { };

template <typename T>
struct is_unsigned_integer : integral_constant<bool, is_integral<T>::value && (T(0) < T(-1))> { };

// is_standard_layout is a builtin
template<typename T>
struct is_standard_layout : integral_constant<bool, __is_standard_layout(T)> { };

}  // namespace mxtl
