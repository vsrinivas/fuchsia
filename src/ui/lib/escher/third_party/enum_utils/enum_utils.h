//  __  __             _        ______                          _____
// |  \/  |           (_)      |  ____|                        / ____|_     _
// | \  / | __ _  __ _ _  ___  | |__   _ __  _   _ _ __ ___   | |   _| |_ _| |_
// | |\/| |/ _` |/ _` | |/ __| |  __| | '_ \| | | | '_ ` _ \  | |  |_   _|_   _|
// | |  | | (_| | (_| | | (__  | |____| | | | |_| | | | | | | | |____|_|   |_|
// |_|  |_|\__,_|\__, |_|\___| |______|_| |_|\__,_|_| |_| |_|  \_____|
//                __/ | https://github.com/Neargye/magic_enum
//               |___/  version 0.6.6
//
// Licensed under the MIT License <http://opensource.org/licenses/MIT>.
// SPDX-License-Identifier: MIT
// Copyright (c) 2019 - 2020 Daniil Goncharov <neargye@gmail.com>.
//
// Permission is hereby  granted, free of charge, to any  person obtaining a copy
// of this software and associated  documentation files (the "Software"), to deal
// in the Software  without restriction, including without  limitation the rights
// to  use, copy,  modify, merge,  publish, distribute,  sublicense, and/or  sell
// copies  of  the Software,  and  to  permit persons  to  whom  the Software  is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE  IS PROVIDED "AS  IS", WITHOUT WARRANTY  OF ANY KIND,  EXPRESS OR
// IMPLIED,  INCLUDING BUT  NOT  LIMITED TO  THE  WARRANTIES OF  MERCHANTABILITY,
// FITNESS FOR  A PARTICULAR PURPOSE AND  NONINFRINGEMENT. IN NO EVENT  SHALL THE
// AUTHORS  OR COPYRIGHT  HOLDERS  BE  LIABLE FOR  ANY  CLAIM,  DAMAGES OR  OTHER
// LIABILITY, WHETHER IN AN ACTION OF  CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE  OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
// Modified based on the following file in the original repository:
// - include/magic_enum.hpp
//
#ifndef SRC_UI_LIB_ESCHER_THIRD_PARTY_ENUM_UTILS_ENUM_UTILS_H_
#define SRC_UI_LIB_ESCHER_THIRD_PARTY_ENUM_UTILS_ENUM_UTILS_H_

#include <algorithm>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace escher::enum_utils {

namespace internal {

// Check if |V| is a valid enum value of type E.
//
// This uses the compiler's __PRETTY_FUNCTION__ feature: For non-enum values,
// the value of __PRETTY_FUNCTION__ looks like:
//   "constexpr bool IsValidEnum() [with E = Enum; E V = (Enum)0]"
// However, for defined enum values, the value looks like:
//   "constexpr bool IsValidEnum() [with E = Enum; E V = Enum::IDENTIFIER]"
//
// We can check whether if the value of |V| is displayed as an identifier in the
// __PRETTY_FUNCTION__ string to know whether it is a valid enum value.
template <typename E, E V>
constexpr bool IsValidEnum() {
  std::string_view name{__PRETTY_FUNCTION__, sizeof(__PRETTY_FUNCTION__) - 2};

  for (std::size_t i = name.size(); i > 0; --i) {
    if (!((name[i - 1] >= '0' && name[i - 1] <= '9') ||
          (name[i - 1] >= 'a' && name[i - 1] <= 'z') ||
          (name[i - 1] >= 'A' && name[i - 1] <= 'Z') || (name[i - 1] == '_'))) {
      name.remove_prefix(i);
      break;
    }
  }

  return !name.empty() && ((name.front() >= 'a' && name.front() <= 'z') ||
                           (name.front() >= 'A' && name.front() <= 'Z') || (name.front() == '_'));
}

// Helper function check if |Begin + Offset| is a valid enum value in |E|.
template <typename E, auto Begin, auto Offset>
constexpr bool IsValueValidEnum() {
  return IsValidEnum<E, static_cast<E>(Begin + Offset)>();
}

// Helper function counting all the valid enum values in integer sequence
// {Begin, Begin + 1, ..., Begin + Ints.size() - 1}.
template <typename E, auto Begin, auto... Ints>
constexpr size_t CountEnumElementInSequence(std::integer_sequence<decltype(Begin), Ints...> seq) {
  return (0ul + ... + IsValueValidEnum<E, Begin, Ints>());
}

// Helper function finding the maximum enum values in integer sequence
// {Begin, Begin + 1, ..., Begin + Ints.size() - 1}.
//
// Returns std::nullopt if none of the values is valid in the sequence.
template <typename E, auto Min, auto... Ints>
constexpr std::optional<decltype(Min)> MaxEnumElementInSequence(
    std::integer_sequence<decltype(Min), Ints...> seq) {
  auto values = {(IsValueValidEnum<E, Min, Ints>())...};
  for (auto it = std::rbegin(values); it != std::rend(values); it++) {
    if (*it) {
      return std::make_optional<decltype(Min)>(Min + (std::rend(values) - it) - 1);
    }
  }
  return std::nullopt;
}

// Helper function finding the minimum enum values in integer sequence
// {Begin, Begin + 1, ..., Begin + Ints.size() - 1}.
//
// Returns std::nullopt if none of the values is valid in the sequence.
template <typename E, auto Min, auto... Ints>
constexpr std::optional<decltype(Min)> MinEnumElementInSequence(
    std::integer_sequence<decltype(Min), Ints...> seq) {
  auto values = {(IsValueValidEnum<E, Min, Ints>())...};
  for (auto it = std::begin(values); it != std::end(values); it++) {
    if (*it) {
      return std::make_optional<decltype(Min)>(Min + (it - std::begin(values)));
    }
  }
  return std::nullopt;
}

}  // namespace internal

// The enum value check uses clang/gcc's __PRETTY_FUNCTION__ feature, and only
// certain versions of the compilers are supported.
constexpr bool IsSupported() {
#if defined(__clang__) && __clang_major__ >= 5 || defined(__GNUC__) && __GNUC__ >= 9
  return true;
#else
  return false;
#endif
}

constexpr auto kDefaultEnumValueBegin = -128;
constexpr auto kDefaultEnumValueEnd = 128;

// Count number of elements in an enum / enum class type |E|.
//
// This compile-time function checks all the values in sequence {Begin,
// Begin + 1, ..., End - 1} to count all defined enum values at compilation
// time.
template <typename E, auto Begin = kDefaultEnumValueBegin, auto End = kDefaultEnumValueEnd,
          auto... Ints>
constexpr size_t CountEnumElement() {
  static_assert(std::is_enum<E>(), "Non-enum type is not supported!");
  static_assert(End > Begin);
  static_assert(IsSupported(), "enum_utils is not supported by the compiler!");
  return internal::CountEnumElementInSequence<E, Begin>(
      std::make_integer_sequence<int, End - Begin>{});
}

// Get the maximum element in an enum / enum class type |E|.
//
// This compile-time function checks all the values in sequence {Begin,
// Begin + 1, ..., End - 1} to find the maximum defined enum values at
// compilation time.
template <typename E, auto Begin = kDefaultEnumValueBegin, auto End = kDefaultEnumValueEnd,
          auto... Ints>
constexpr std::optional<decltype(Begin)> MaxEnumElementValue() {
  static_assert(std::is_enum<E>(), "Non-enum type is not supported!");
  static_assert(End > Begin);
  static_assert(IsSupported(), "enum_utils is not supported by the compiler!");
  return internal::MaxEnumElementInSequence<E, Begin>(
      std::make_integer_sequence<int, End - Begin>{});
}

// Get the minimum element in an enum / enum class type |E|.
//
// This compile-time function checks all the values in sequence {Begin,
// Begin + 1, ..., End - 1} to find the minimum defined enum values at
// compilation time.
template <typename E, auto Begin = kDefaultEnumValueBegin, auto End = kDefaultEnumValueEnd,
          auto... Ints>
constexpr std::optional<decltype(Begin)> MinEnumElementValue() {
  static_assert(std::is_enum<E>(), "Non-enum type is not supported!");
  static_assert(End > Begin);
  static_assert(IsSupported(), "enum_utils is not supported by the compiler!");
  return internal::MinEnumElementInSequence<E, Begin>(
      std::make_integer_sequence<int, End - Begin>{});
}

}  // namespace escher::enum_utils

#endif  // SRC_UI_LIB_ESCHER_THIRD_PARTY_ENUM_UTILS_ENUM_UTILS_H_
