// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DIAGNOSTICS_PRINTF_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DIAGNOSTICS_PRINTF_H_

#include <inttypes.h>

#include <string_view>
#include <tuple>
#include <type_traits>

#include "const-string.h"

namespace elfldltl {

template <typename T>
struct FileOffset;

template <typename T>
struct FileAddress;

namespace internal {

// This only exists to be specialized.  The interface is shown here.
template <typename T>
struct PrintfType {
  static_assert(std::is_void_v<T>, "missing specialization");

  // This is a ConstString of a printf format string fragment.
  static constexpr auto kFormat = ConstString("%something");

  // This is a function of T that returns a std::tuple<...> of the arguments to
  // pass to printf corresponding to the kFormat string.
  static constexpr auto Arguments(T arg) { return std::make_tuple(arg); }
};

template <typename T>
struct PrintfType<T&> : public PrintfType<T> {};

template <typename T>
struct PrintfType<const T&> : public PrintfType<T> {};

template <typename T>
struct PrintfType<T&&> : public PrintfType<T> {};

template <>
struct PrintfType<uint8_t> {
  static constexpr auto kFormat = ConstString(" %" PRIu8);
  static constexpr auto Arguments(uint8_t arg) { return std::make_tuple(arg); }
};

template <>
struct PrintfType<uint16_t> {
  static constexpr auto kFormat = ConstString(" %" PRIu16);
  static constexpr auto Arguments(uint16_t arg) { return std::make_tuple(arg); }
};

template <>
struct PrintfType<uint32_t> {
  static constexpr auto kFormat = ConstString(" %" PRIu32);
  static constexpr auto Arguments(uint32_t arg) { return std::make_tuple(arg); }
};

template <>
struct PrintfType<uint64_t> {
  static constexpr auto kFormat = ConstString(" %" PRIu64);
  static constexpr auto Arguments(uint64_t arg) { return std::make_tuple(arg); }
};

template <typename T>
constexpr auto PrintfHexFormatStringForType() {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return ConstString(" %#" PRIx8);
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    return ConstString(" %#" PRIx16);
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    return ConstString(" %#" PRIx32);
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    return ConstString(" %#" PRIx64);
  } else {
    static_assert(std::is_void_v<T>, "unhandled integer type??");
    return ConstString(" elfldltl BUG!");
  }
}

// This handles string literals.  It could fold them into the format
// string, but that would require doubling any '%' inside.
template <size_t N>
struct PrintfType<const char (&)[N]> {
  static constexpr auto kFormat = ConstString("%s");
  static constexpr auto Arguments(const char (&str)[N]) { return std::forward_as_tuple(str); }
};

template <size_t Len>
struct PrintfType<ConstString<Len>> {
  static constexpr auto kFormat = ConstString("%s");
  static constexpr auto Arguments(const ConstString<Len>& str) {
    return std::make_tuple(str.c_str());
  }
};

template <>
struct PrintfType<const char*> {
  static constexpr auto kFormat = ConstString("%s");
  static constexpr auto Arguments(const char* str) { return std::make_tuple(str); }
};

template <>
struct PrintfType<std::string_view> {
  static constexpr auto kFormat = ConstString("%.*s");
  static constexpr auto Arguments(std::string_view str) {
    return std::make_tuple(static_cast<int>(str.size()), str.data());
  }
};

template <typename T>
struct PrintfType<FileOffset<T>> {
  static constexpr auto kFormat =
      ConstString(" at file offset") + PrintfHexFormatStringForType<T>();
  static constexpr auto Arguments(FileOffset<T> arg) { return std::make_tuple(*arg); }
};

template <typename T>
struct PrintfType<FileAddress<T>> {
  static constexpr auto kFormat =
      ConstString(" at relative address") + PrintfHexFormatStringForType<T>();
  static constexpr auto Arguments(FileAddress<T> arg) { return std::make_tuple(*arg); }
};

// This concatenates them all together in a mandatory constexpr context so the
// whole format string becomes effectively a single string literal.
template <typename... T>
inline constexpr auto kPrintfFormat = (PrintfType<T>::kFormat + ...);

// Specialize the empty case.
template <>
inline constexpr auto kPrintfFormat<> = ConstString("");

// Calls printer("format string", ...) with arguments corresponding to
// prefix..., args... (each prefix argument and each later argument might
// produce multiple arguments to printer).
template <typename Printer, typename... Prefix, typename... Args>
constexpr void Printf(Printer&& printer, std::tuple<Prefix...> prefix, Args&&... args) {
  constexpr auto printer_args = [](auto&&... args) {
    constexpr auto kFormat = kPrintfFormat<decltype(args)...>.c_str();
    constexpr auto arg_tuple = [](auto&& arg) {
      using T = decltype(arg);
      return PrintfType<T>::Arguments(std::forward<T>(arg));
    };
    return std::tuple_cat(std::make_tuple(kFormat),
                          arg_tuple(std::forward<decltype(args)>(args))...);
  };
  std::apply(
      std::forward<Printer>(printer),
      std::apply(printer_args, std::tuple_cat(std::move(prefix),
                                              std::forward_as_tuple(std::forward<Args>(args)...))));
}

}  // namespace internal
}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_DIAGNOSTICS_PRINTF_H_
