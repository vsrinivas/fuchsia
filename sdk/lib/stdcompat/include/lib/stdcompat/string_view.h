// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_STRING_VIEW_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_STRING_VIEW_H_

#include <cstddef>
#include <stdexcept>

#include "version.h"

#if defined(__cpp_lib_string_view) && __cpp_lib_string_view >= 201606L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

#include <string_view>

namespace cpp17 {

using std::basic_string_view;
using std::string_view;
using std::u16string_view;
using std::u32string_view;
using std::wstring_view;

}  // namespace cpp17

#else  // Provide polyfills for the types provided by <string_view>.

#include <cassert>
#include <cstdlib>
#include <exception>
#include <ios>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>

#include "internal/exception.h"
#include "memory.h"
#include "utility.h"

namespace cpp17 {

// Provides a view to a sequence of characters.
template <class CharT, class Traits = std::char_traits<CharT>>
class basic_string_view {
 public:
  using traits_type = Traits;
  using value_type = CharT;
  using pointer = CharT*;
  using const_pointer = const CharT*;
  using reference = CharT&;
  using const_reference = const CharT&;
  using iterator = const_pointer;
  using const_iterator = iterator;
  using reverse_iterator = std::reverse_iterator<const_iterator>;
  using const_reverse_iterator = reverse_iterator;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;

  static constexpr size_type npos = static_cast<size_type>(-1);

  constexpr basic_string_view() noexcept : data_(nullptr), length_(0) {}
  constexpr basic_string_view(const CharT* s, size_type count) noexcept
      : data_(s), length_(count) {}
  constexpr basic_string_view(const CharT* s) noexcept : data_(s), length_(Traits::length(s)) {}
  template <typename Allocator>
  basic_string_view(const std::basic_string<CharT, Traits, Allocator>& s) noexcept
      : data_(s.data()), length_(s.length()) {}
  constexpr basic_string_view(const basic_string_view& other) noexcept = default;
  basic_string_view(basic_string_view&&) noexcept = default;
  constexpr basic_string_view& operator=(const basic_string_view& view) noexcept = default;
  constexpr basic_string_view& operator=(basic_string_view&&) noexcept = default;
  ~basic_string_view() = default;

  constexpr iterator begin() const { return data_; }
  constexpr iterator end() const { return begin() + length_; }
  constexpr const_iterator cbegin() const { return data_; }
  constexpr const_iterator cend() const { return begin() + length_; }

  constexpr reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  constexpr reverse_iterator rend() const { return const_reverse_iterator(begin()); }
  constexpr const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }
  constexpr const_reverse_iterator crend() const { return const_reverse_iterator(begin()); }

  constexpr const_pointer data() const { return data_; }
  constexpr size_type size() const { return length_; }
  constexpr size_type length() const { return length_; }
  constexpr size_type max_size() const { return std::numeric_limits<size_type>::max(); }

  constexpr const_reference front() const { return this->operator[](0); }
  constexpr const_reference back() const { return this->operator[](size() - 1); }
  constexpr bool empty() const { return size() == 0; }

  constexpr const_reference operator[](size_type pos) const {
    assert(pos < size());
    return *(data() + pos);
  }

  constexpr const_reference at(size_type pos) const {
    if (pos >= size()) {
      internal::throw_or_abort<std::out_of_range>(
          "Index out of bounds for basic_stirng_view<T>::at");
    }
    return this->operator[](pos);
  }

  constexpr void remove_prefix(size_type n) {
    assert(n <= size());
    data_ += n;
    length_ -= n;
  }
  constexpr void remove_suffix(size_type n) {
    assert(n <= size());
    length_ -= n;
  }

  constexpr void swap(basic_string_view& other) noexcept {
    other.data_ = cpp20::exchange(data_, std::move(other.data_));
    other.length_ = cpp20::exchange(length_, std::move(other.length_));
  }

  size_type copy(CharT* dest, size_type count, size_type pos = 0) const {
    if (pos > size()) {
      internal::throw_or_abort<std::out_of_range>(
          "Index out of bounds for basic_string_view<>::copy.");
    }
    Traits::copy(dest, data() + pos, calculate_length(pos, count));
    return count;
  }

  constexpr basic_string_view substr(size_type pos = 0, size_type count = npos) const {
    if (pos > size()) {
      internal::throw_or_abort<std::out_of_range>(
          "Index out of bounds for basic_string_view<>::substr.");
    }
    return basic_string_view(data() + pos, calculate_length(pos, count));
  }

  constexpr int compare(basic_string_view v) const {
    const size_type count = std::min(length(), v.length());
    for (std::size_t curr = 0; curr < count; ++curr) {
      // Exit as soon as we find a different character.
      if (std::char_traits<CharT>::lt(at(curr), v[curr])) {
        return -1;
      }

      if (!std::char_traits<CharT>::eq(at(curr), v[curr])) {
        return 1;
      }
    }

    // All characters in the range match.
    return static_cast<int>(length() - v.length());
  }

  constexpr int compare(size_type pos1, size_type count1, basic_string_view v) const {
    return substr(pos1, count1).compare(v);
  }

  constexpr int compare(size_type pos1, size_type count1, basic_string_view v, size_type pos2,
                        size_type count2) const {
    return substr(pos1, count1).compare(v.substr(pos2, count2));
  }

  constexpr int compare(const CharT* s) const { return compare(basic_string_view(s)); }

  constexpr int compare(size_type pos1, size_type count1, const CharT* s) const {
    return substr(pos1, count1).compare(basic_string_view(s));
  }

  constexpr int compare(size_type pos1, size_type count1, const CharT* s, size_type count2) const {
    return substr(pos1, count1).compare(basic_string_view(s, count2));
  }

  constexpr size_type find(basic_string_view v, size_type pos = 0) const noexcept {
    pos = std::min(pos, length());
    if (v.empty()) {
      return pos;
    }

    if (length() - pos < v.length()) {
      return npos;
    }

    auto target_haystack = substr(pos);
    const auto max_search_length = target_haystack.length() - v.length() + 1;
    for (size_type i = 0; i < max_search_length; ++i) {
      // Look for the needle front before comparing the full string.
      while (!Traits::eq(target_haystack[i], v[0])) {
        ++i;
        if (i >= max_search_length) {
          return npos;
        }
      }
      if (target_haystack.compare(i, v.length(), v) == 0) {
        return pos + i;
      }
    }

    return npos;
  }

  constexpr size_type find(CharT ch, size_type pos = 0) const {
    return find(basic_string_view(addressof(ch), 1), pos);
  }

  constexpr size_type find(const CharT* s, size_type pos, size_type count) const {
    return find(basic_string_view(s, count), pos);
  }

  constexpr size_type find(const CharT* s, size_type pos) const {
    return find(basic_string_view(s), pos);
  }

  constexpr size_type rfind(basic_string_view v, size_type pos = npos) const noexcept {
    pos = std::min(pos, length());
    if (v.empty()) {
      return pos;
    }

    if (length() < v.length()) {
      return npos;
    }

    auto target_haystack = substr(0, (pos + 1) - (v.length() - 1));
    for (size_type i = 0; i < target_haystack.length(); ++i) {
      auto reverse_i = target_haystack.length() - i - 1;
      while (!Traits::eq(target_haystack[reverse_i], v[0])) {
        ++i;
        --reverse_i;
        if (i >= target_haystack.length()) {
          return npos;
        }
      }
      if (compare(reverse_i, v.length(), v) == 0) {
        return reverse_i;
      }
    }

    return npos;
  }

  constexpr size_type rfind(CharT ch, size_type pos = npos) const {
    return rfind(basic_string_view(addressof(ch), 1), pos);
  }

  constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const {
    return rfind(basic_string_view(s, count), pos);
  }

  constexpr size_type rfind(const CharT* s, size_type pos) const {
    return rfind(basic_string_view(s), pos);
  }

  constexpr size_type find_first_of(basic_string_view v, size_type pos = 0) const noexcept {
    pos = std::min(length(), pos);
    for (auto it = begin() + pos; it != end(); ++it) {
      if (v.find(*it) != npos) {
        return std::distance(begin(), it);
      }
    }

    return npos;
  }

  constexpr size_type find_first_of(CharT c, size_type pos = 0) const noexcept {
    return find_first_of(basic_string_view(addressof(c), 1), pos);
  }

  constexpr size_type find_first_of(const CharT* s, size_type pos, size_type count) const {
    return find_first_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_first_of(const CharT* s, size_type pos = 0) const {
    return find_first_of(basic_string_view(s), pos);
  }

  constexpr size_type find_last_of(basic_string_view v,
                                   size_type pos = basic_string_view::npos) const noexcept {
    if (empty()) {
      return npos;
    }

    pos = length() - std::min(pos, length() - 1) - 1;
    for (auto it = rbegin() + pos; it != rend(); ++it) {
      if (v.find(*it) != npos) {
        return length() - 1 - std::distance(rbegin(), it);
      }
    }

    return npos;
  }

  constexpr size_type find_last_of(CharT c,
                                   size_type pos = basic_string_view::npos) const noexcept {
    return find_last_of(basic_string_view(addressof(c), 1), pos);
  }

  constexpr size_type find_last_of(const CharT* s, size_type pos, size_type count) const {
    return find_last_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_last_of(const CharT* s, size_type pos = basic_string_view::npos) const {
    return find_last_of(basic_string_view(s), pos);
  }

  constexpr size_type find_first_not_of(basic_string_view v, size_type pos = 0) const noexcept {
    pos = std::min(pos, length());
    for (auto it = begin() + pos; it != end(); ++it) {
      if (v.find(*it) == npos) {
        return std::distance(begin(), it);
      }
    }
    return npos;
  }

  constexpr size_type find_first_not_of(CharT c, size_type pos = 0) const noexcept {
    return find_first_not_of(basic_string_view(addressof(c), 1), pos);
  }

  constexpr size_type find_first_not_of(const CharT* s, size_type pos, size_type count) const {
    return find_first_not_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_first_not_of(const CharT* s, size_type pos = 0) const {
    return find_first_not_of(basic_string_view(s), pos);
  }

  constexpr size_type find_last_not_of(basic_string_view v,
                                       size_type pos = basic_string_view::npos) const noexcept {
    if (empty()) {
      return npos;
    }

    pos = length() - std::min(pos, length() - 1) - 1;
    for (auto it = rbegin() + pos; it != rend(); ++it) {
      if (v.find(*it) == npos) {
        return length() - 1 - std::distance(rbegin(), it);
      }
    }
    return npos;
  }

  constexpr size_type find_last_not_of(CharT c,
                                       size_type pos = basic_string_view::npos) const noexcept {
    return find_last_not_of(basic_string_view(addressof(c), 1), pos);
  }

  constexpr size_type find_last_not_of(const CharT* s, size_type pos, size_type count) const {
    return find_last_not_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_last_not_of(const CharT* s,
                                       size_type pos = basic_string_view::npos) const {
    return find_last_not_of(basic_string_view(s), pos);
  }

 private:
  constexpr size_type calculate_length(size_type pos, size_type count) const {
    if (count == npos) {
      count = size();
    }
    return std::min(count, size() - pos);
  }

  const_pointer data_ = nullptr;
  size_type length_ = 0;
};

// Operators and overloads to satisfy all conversions.
//
// Defined overloads are of the form:
//   <basic_string_view, basic_string_view>
//   <RawType, basic_string_view>
//   <basic_string_view, RawType>
//
// When |RawType| is lhs: std::is_constructible<basic_string_view, RawType>::value must be true.
// When |RawType| is rhs: There must be an overload of basic_string_view::compare for |RawType|.
template <class CharT, class Traits>
constexpr bool operator==(cpp17::basic_string_view<CharT, Traits> lhs,
                          cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) == 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator==(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) == 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator==(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) == 0;
}

template <class CharT, class Traits>
constexpr bool operator!=(cpp17::basic_string_view<CharT, Traits> lhs,
                          cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) != 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator!=(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) != 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator!=(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) != 0;
}

template <class CharT, class Traits>
constexpr bool operator<(cpp17::basic_string_view<CharT, Traits> lhs,
                         cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) < 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) < 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) < 0;
}

template <class CharT, class Traits>
constexpr bool operator>(cpp17::basic_string_view<CharT, Traits> lhs,
                         cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) > 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) > 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) > 0;
}

template <class CharT, class Traits>
constexpr bool operator<=(cpp17::basic_string_view<CharT, Traits> lhs,
                          cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) <= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<=(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) <= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<=(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) <= 0;
}

template <class CharT, class Traits>
constexpr bool operator>=(cpp17::basic_string_view<CharT, Traits> lhs,
                          cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) >= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>=(RawType lhs, cpp17::basic_string_view<CharT, Traits> rhs) noexcept {
  return cpp17::basic_string_view<CharT, Traits>(lhs).compare(rhs) >= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>=(cpp17::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) >= 0;
}

// Specializations.
using string_view = cpp17::basic_string_view<char>;
using wstring_view = cpp17::basic_string_view<wchar_t>;
using u16string_view = cpp17::basic_string_view<char16_t>;
using u32string_view = cpp17::basic_string_view<char32_t>;

}  // namespace cpp17

namespace std {
// Hash needs to match basic_string view hash of the same string, so we need to rely on compiler
// implementation.
// https://en.cppreference.com/w/cpp/string/basic_string_view/hash
template <class CharT>
struct hash<cpp17::basic_string_view<CharT, std::char_traits<CharT>>> {
  std::size_t operator()(cpp17::basic_string_view<CharT, std::char_traits<CharT>> val) const {
    return __do_string_hash(val.data(), val.data() + val.length());
  }
};

// Output stream specialization for cpp17::string_view.
//
// https://en.cppreference.com/w/cpp/string/basic_string_view/operator_ltlt
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os,
                                              cpp17::basic_string_view<CharT, Traits> v) {
  using size_type = typename cpp17::basic_string_view<CharT, Traits>::size_type;
  const size_type sequence_length = std::max(static_cast<size_type>(os.width()), v.length());
  const size_type fill_sequence =
      (static_cast<size_type>(os.width()) > v.length()) ? os.width() - v.length() : 0;
  os.width(sequence_length);
  auto fill_space = [](std::basic_ostream<CharT, Traits>& os, size_type fill_length) {
    for (std::size_t i = 0; i < fill_length; ++i) {
      os.put(os.fill());
    }
  };

  bool should_fill_left = (os.flags() & std::ios_base::adjustfield) == std::ios_base::left;

  if (!should_fill_left) {
    fill_space(os, fill_sequence);
  }

  os.write(v.data(), v.length());

  if (should_fill_left) {
    fill_space(os, fill_sequence);
  }

  os.width(0);

  return os;
}

}  // namespace std
#endif  // __cpp_lib_string_view >= 201606L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

// Per the README, we define standalone cpp20::starts_with() and
// cpp20::ends_with() functions. These correspond to std::basic_string_view
// methods introduced in C++20. For parity's sake, in the C++20 context we also
// define the same functions, though as thin wrappers around these methods.
namespace cpp20 {

#if defined(__cpp_lib_string_view) && __cpp_lib_string_view >= 202002L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

template <class CharT, class Traits = std::char_traits<CharT>, typename PrefixType>
constexpr bool starts_with(cpp17::basic_string_view<CharT, Traits> s,
                           std::decay_t<PrefixType> prefix) {
  return s.starts_with(prefix);
}

template <class CharT, class Traits = std::char_traits<CharT>, typename SuffixType>
constexpr bool ends_with(cpp17::basic_string_view<CharT, Traits> s,
                         std::decay_t<SuffixType> suffix) {
  return s.ends_with(suffix);
}

#else  // Polyfills for C++20 std::basic_string_view methods.

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool starts_with(cpp17::basic_string_view<CharT, Traits> s, decltype(s) prefix) {
  return s.substr(0, prefix.size()) == prefix;
}

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool starts_with(cpp17::basic_string_view<CharT, Traits> s, const CharT* prefix) {
  return starts_with(s, decltype(s){prefix});
}

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool starts_with(cpp17::basic_string_view<CharT, Traits> s, CharT c) {
  return !s.empty() && Traits::eq(s.front(), c);
}

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool ends_with(cpp17::basic_string_view<CharT, Traits> s, decltype(s) suffix) {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size(), suffix.size()) == suffix;
}

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool ends_with(cpp17::basic_string_view<CharT, Traits> s, const CharT* suffix) {
  return ends_with(s, decltype(s){suffix});
}

template <class CharT, class Traits = std::char_traits<CharT>>
constexpr bool ends_with(cpp17::basic_string_view<CharT, Traits> s, CharT c) {
  return !s.empty() && Traits::eq(s.back(), c);
}

#endif  // if __cpp_lib_string_view >= 202002L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace cpp20

namespace cpp17 {
// Constructs a string_view from ""_sv literal.
// Literals with no leading underscore are reserved for the standard library.
// https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv
//
// This is unconditionally defined in this header, so '_sv' is available independently whether the
// polyfills are being used or just aliasing the std ones.
inline namespace literals {
inline namespace string_view_literals {

constexpr cpp17::string_view operator"" _sv(typename cpp17::string_view::const_pointer str,
                                            typename cpp17::string_view::size_type len) noexcept {
  return cpp17::string_view(str, len);
}

constexpr cpp17::wstring_view operator"" _sv(typename cpp17::wstring_view::const_pointer str,
                                             typename cpp17::wstring_view::size_type len) noexcept {
  return cpp17::wstring_view(str, len);
}

constexpr cpp17::u16string_view operator"" _sv(
    typename cpp17::u16string_view::const_pointer str,
    typename cpp17::u16string_view::size_type len) noexcept {
  return cpp17::u16string_view(str, len);
}

constexpr cpp17::u32string_view operator"" _sv(
    typename cpp17::u32string_view::const_pointer str,
    typename cpp17::u32string_view::size_type len) noexcept {
  return cpp17::u32string_view(str, len);
}

}  // namespace string_view_literals
}  // namespace literals
}  // namespace cpp17

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_STRING_VIEW_H_
