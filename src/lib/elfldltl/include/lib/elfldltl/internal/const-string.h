// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_CONST_STRING_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_CONST_STRING_H_

#include <array>
#include <functional>
#include <string_view>
#include <type_traits>

namespace elfldltl::internal {

// This is a poor programmer's constexpr std::string (which C++ 20 has).
// It provides the barest subset of std::string functionality in constexpr:
//  * Copy-constructible, constructible from string literals
//  * Convertible to std::string_view, has those common methods
//  * Has c_str() method with NUL terminator guarantee
//  * Has operator+ that takes other ConstString types or string literals
// So essentially it acts as an immutable std::string that can be used in
// simple functional style.

template <size_t Len>
class ConstString {
 public:
  template <size_t N>
  using ConstStringRef = const char (&)[N];

  using Literal = ConstStringRef<Len + 1>;

  constexpr ConstString(Literal other) { Appender{str_.data()}(other); }

  template <size_t... N>
  explicit constexpr ConstString(ConstStringRef<N>... literals) {
    static_assert(((N - 1) + ...) == Len);
    Appender append{str_.data()};
    (append(literals), ...);
  }

  template <size_t... OtherLen>
  explicit constexpr ConstString(const ConstString<OtherLen>&... other) {
    static_assert((OtherLen + ...) == Len);
    Appender append{str_.data()};
    (append(other), ...);
  }

  constexpr const char* data() const { return str_.data(); }

  constexpr size_t size() const { return Len; }

  constexpr size_t empty() const { return size() == 0; }

  constexpr const char* c_str() const { return str_.data(); }

  constexpr const char* begin() const { return data(); }

  constexpr const char* end() const { return data() + size(); }

  constexpr operator std::string_view() const { return {data(), size()}; }

  template <size_t OtherLen>
  constexpr auto operator+(const ConstString<OtherLen>& other) const {
    return ConstString<Len + OtherLen>(*this, other);
  }

  template <size_t OtherLenWithTerminator>
  constexpr auto operator+(const char (&other)[OtherLenWithTerminator]) const {
    return *this + ConstString<OtherLenWithTerminator - 1>(other);
  }

  template <size_t OtherLen>
  constexpr bool operator==(const ConstString<OtherLen>& other) const {
    return static_cast<std::string_view>(*this) == other;
  }

  template <auto N, unsigned int Base>
  friend constexpr auto IntegerConstString();

 private:
  constexpr ConstString() = default;

  struct Appender {
    template <size_t OtherLen>
    constexpr void operator()(const ConstString<OtherLen>& str) {
      for (char c : str) {
        *p_++ = c;
      }
    }

    constexpr void operator()(std::string_view str) {
      for (char c : str) {
        *p_++ = c;
      }
    }

    char* p_ = nullptr;
  };

  std::array<char, Len + 1> str_{};
};

template <size_t Len>
ConstString(ConstString<Len> other) -> ConstString<Len>;

template <size_t LiteralLen>
ConstString(const char (&literal)[LiteralLen]) -> ConstString<LiteralLen - 1>;

template <typename T>
constexpr size_t IntegerDigits(T n, unsigned int base) {
  size_t digits = 1;
  while (n >= static_cast<T>(base)) {
    n /= static_cast<T>(base);
    ++digits;
  }
  return digits;
}

template <auto N, unsigned int Base = 10>
constexpr auto IntegerConstString() {
  static_assert(N >= 0);
  constexpr size_t kDigits = IntegerDigits(N, Base);
  ConstString<kDigits> result;
  auto n = N;
  auto it = result.str_.rbegin();
  *it++ = '\0';
  for (auto end = result.str_.rend(); it != end; it++) {
    char& c = *it;
    static_assert(Base <= 16);
    c = "0123456789abcdef"[n % Base];
    n /= Base;
  }
  return result;
}

static_assert(IntegerConstString<1234567>() == ConstString("1234567"));

}  // namespace elfldltl::internal

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_INTERNAL_CONST_STRING_H_
