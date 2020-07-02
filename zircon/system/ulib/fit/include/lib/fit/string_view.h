// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIT_STRING_VIEW_H_
#define LIB_FIT_STRING_VIEW_H_

#include <cassert>
#include <cstdlib>
#include <ios>
#include <iterator>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

namespace fit {

namespace internal {

// Constexpr filler for std::swap. No specialization for arrays.
template <typename T>
constexpr void constexpr_swap(T& a, T& b) noexcept {
  T tmp = std::move(a);
  a = std::move(b);
  b = std::move(tmp);
}

// Constexpr filler for C++17 std::addressof.
template <typename T>
constexpr T* addressof(T& arg) {
  return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(arg)));
}

// Filler for char_traits<CharT>::compare
template <typename CharT>
constexpr int compare(const CharT* s1, const CharT* s2, std::size_t count) {
  for (std::size_t curr = 0; curr < count; ++curr) {
    // Exit as soon as we find a different character.
    if (std::char_traits<CharT>::lt(s1[curr], s2[curr])) {
      return -1;
    } else if (!std::char_traits<CharT>::eq(s1[curr], s2[curr])) {
      return 1;
    }
  }
  // If all characters within [s1, s1+count) and [s2, s2+count) are equal
  // return 0.
  return 0;
}

// Returns the distance from |begin| to first character in [|it|, |end|) that is equal to
// |needle.front()|.
// Returns |StringViewType::npos| if no such character is found.
//
// Complexity: O(|std::distance(it, end)|).
template <typename StringViewType, typename Iterator>
typename StringViewType::size_type find_char(Iterator it, Iterator begin, Iterator end,
                                             StringViewType needle) {
  // Look starting point.
  while (it != end && !StringViewType::traits_type::eq(*it, needle.front())) {
    ++it;
  }

  if (it == end) {
    return StringViewType::npos;
  }
  return static_cast<typename StringViewType::size_type>(std::distance(begin, it));
}

// Returns the distance from the first character starting from |begin| that matches
// any characters in |matchers|.
// Returns |StringViewType::npos| if no characters are within |matchers|.
//
// Complexity: O(|std::distance(begin, end)|*|matchers.length()|).
template <typename StringViewType, typename Iterator>
constexpr typename StringViewType::size_type find_first_of(Iterator begin, Iterator end,
                                                           StringViewType matchers) {
  typename StringViewType::size_type curr = 0;
  for (Iterator it = begin; it < end; ++it) {
    for (const auto& matcher : matchers) {
      if (StringViewType::traits_type::eq(*it, matcher)) {
        return curr;
      }
    }
    ++curr;
  }

  return StringViewType::npos;
}

// Returns the distance from the first character starting from |begin| that does not match
// any characters in |matchers|.
// Returns |StringViewType::npos| if all characters are within |matchers|.
//
// Complexity: O(|std::distance(begin, end)|*|matchers.length()|).
template <typename StringViewType, typename Iterator>
constexpr typename StringViewType::size_type find_first_not_of(Iterator begin, Iterator end,
                                                               StringViewType matchers) {
  typename StringViewType::size_type curr = 0;

  for (Iterator it = begin; it < end; ++it) {
    bool matched = false;
    for (const auto& matcher : matchers) {
      if (StringViewType::traits_type::eq(*it, matcher)) {
        matched = true;
        break;
      }
    }

    if (!matched) {
      return curr;
    }
    ++curr;
  }

  return StringViewType::npos;
}

// Returns the starting point of |needle| within |haystack|.
// If no match is found, return |StringViewType::npos|.
//
// Complexity: O(|std::distance(begin, end)| * |needle.length()|)
template <typename StringViewType, typename Iterator>
constexpr typename StringViewType::size_type find(Iterator begin, Iterator end,
                                                  const StringViewType needle) {
  // If the needle does not fit in the haystack, there is no possible match.
  if (static_cast<typename StringViewType::size_type>(std::distance(begin, end)) <
      needle.length()) {
    return StringViewType::npos;
  }

  if (needle.empty()) {
    return 0;
  }

  Iterator it = begin;

  while (it < end) {
    typename StringViewType::size_type offset = find_char(it, begin, end, needle);
    // If no match discard.
    if (offset == StringViewType::npos) {
      return StringViewType::npos;
    }
    it = begin + offset;

    if (internal::compare<typename StringViewType::value_type>(&(*it), needle.data(),
                                                               needle.size()) == 0) {
      return std::distance(begin, it);
    }
    ++it;
  }

  // We did not find the substring in the haystack.
  return StringViewType::npos;
}

}  // namespace internal

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

  constexpr const_reference operator[](size_type pos) const { return *(data() + pos); }
  constexpr const_reference at(size_type pos) const {
    assert(pos < size());
    return this->operator[](pos);
  }

  constexpr void remove_prefix(size_type n) {
    data_ += n;
    length_ -= n;
  }
  constexpr void remove_suffix(size_type n) { length_ -= n; }

  constexpr void swap(basic_string_view& other) noexcept {
    internal::constexpr_swap(data_, other.data_);
    internal::constexpr_swap(length_, other.length_);
  }

  size_type copy(CharT* dest, size_type count, size_type pos = 0) const {
    assert(pos < size());
    Traits::copy(dest, data() + pos, calculate_length(pos, count));
    return count;
  }

  constexpr basic_string_view substr(size_type pos = 0, size_type count = npos) const {
    return basic_string_view(data() + pos, calculate_length(pos, count));
  }

  constexpr int compare(basic_string_view v) const {
    const int result = internal::compare(data(), v.data(), std::min(size(), v.size()));
    if (result == 0) {
      return static_cast<int>(size() - v.size());
    }
    return result;
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
    auto tmp = internal::find(substr(pos).begin(), substr(pos).end(), v);
    return (tmp == npos) ? npos : pos + tmp;
  }

  constexpr size_type find(CharT ch, size_type pos = 0) const {
    return find(basic_string_view(internal::addressof(ch), 1), pos);
  }

  constexpr size_type find(const CharT* s, size_type pos, size_type count) const {
    return find(basic_string_view(s, count), pos);
  }

  constexpr size_type find(const CharT* s, size_type pos) const {
    return find(basic_string_view(s), pos);
  }

  constexpr size_type rfind(basic_string_view v, size_type pos = 0) const noexcept {
    auto tmp = internal::find(substr(pos).rbegin(), substr(pos).rend(), v);
    return (tmp == npos) ? npos : length() - 1 - tmp;
  }

  constexpr size_type rfind(CharT ch, size_type pos = 0) const {
    return rfind(basic_string_view(internal::addressof(ch), 1), pos);
  }

  constexpr size_type rfind(const CharT* s, size_type pos, size_type count) const {
    return rfind(basic_string_view(s, count), pos);
  }

  constexpr size_type rfind(const CharT* s, size_type pos) const {
    return rfind(basic_string_view(s), pos);
  }

  constexpr size_type find_first_of(basic_string_view v, size_type pos = 0) const noexcept {
    auto tmp = internal::find_first_of(substr(pos).begin(), substr(pos).end(), v);
    return tmp == npos ? npos : pos + tmp;
  }

  constexpr size_type find_first_of(CharT c, size_type pos = 0) const noexcept {
    return find_first_of(basic_string_view(internal::addressof(c), 1), pos);
  }

  constexpr size_type find_first_of(const CharT* s, size_type pos, size_type count) const {
    return find_first_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_first_of(const CharT* s, size_type pos = 0) const {
    return find_first_of(basic_string_view(s), pos);
  }

  constexpr size_type find_last_of(basic_string_view v,
                                   size_type pos = basic_string_view::npos) const noexcept {
    const size_type fixed_length = (pos == npos) ? size() : pos + 1;
    const size_type tmp = internal::find_first_of(substr(0, fixed_length).rbegin(),
                                                  substr(0, fixed_length).rend(), v);
    return tmp == npos ? npos : fixed_length - 1 - tmp;
  }

  constexpr size_type find_last_of(CharT c, size_type pos = basic_string_view::npos) const
      noexcept {
    return find_last_of(basic_string_view(internal::addressof(c), 1), pos);
  }

  constexpr size_type find_last_of(const CharT* s, size_type pos, size_type count) const {
    return find_last_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_last_of(const CharT* s, size_type pos = basic_string_view::npos) const {
    return find_last_of(basic_string_view(s), pos);
  }

  constexpr size_type find_first_not_of(basic_string_view v, size_type pos = 0) const noexcept {
    const auto tmp = internal::find_first_not_of(substr(pos).begin(), substr(pos).end(), v);
    return tmp == npos ? npos : pos + tmp;
  }

  constexpr size_type find_first_not_of(CharT c, size_type pos = 0) const noexcept {
    return find_first_not_of(basic_string_view(internal::addressof(c), 1), pos);
  }

  constexpr size_type find_first_not_of(const CharT* s, size_type pos, size_type count) const {
    return find_first_not_of(basic_string_view(s, count), pos);
  }

  constexpr size_type find_first_not_of(const CharT* s, size_type pos = 0) const {
    return find_first_not_of(basic_string_view(s), pos);
  }

  constexpr size_type find_last_not_of(basic_string_view v,
                                       size_type pos = basic_string_view::npos) const noexcept {
    const size_type fixed_length = (pos == npos) ? size() : pos + 1;
    auto tmp = internal::find_first_not_of(substr(0, fixed_length).rbegin(),
                                           substr(0, fixed_length).rend(), v);
    return tmp == npos ? npos : fixed_length - 1 - tmp;
  }

  constexpr size_type find_last_not_of(CharT c, size_type pos = basic_string_view::npos) const
      noexcept {
    return find_last_not_of(basic_string_view(internal::addressof(c), 1), pos);
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

  const_pointer data_;
  size_type length_;
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
constexpr bool operator==(fit::basic_string_view<CharT, Traits> lhs,
                          fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) == 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator==(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) == 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator==(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) == 0;
}

template <class CharT, class Traits>
constexpr bool operator!=(fit::basic_string_view<CharT, Traits> lhs,
                          fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) != 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator!=(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) != 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator!=(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) != 0;
}

template <class CharT, class Traits>
constexpr bool operator<(fit::basic_string_view<CharT, Traits> lhs,
                         fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) < 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) < 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) < 0;
}

template <class CharT, class Traits>
constexpr bool operator>(fit::basic_string_view<CharT, Traits> lhs,
                         fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) > 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) > 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) > 0;
}

template <class CharT, class Traits>
constexpr bool operator<=(fit::basic_string_view<CharT, Traits> lhs,
                          fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) <= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<=(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) <= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator<=(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) <= 0;
}

template <class CharT, class Traits>
constexpr bool operator>=(fit::basic_string_view<CharT, Traits> lhs,
                          fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return lhs.compare(rhs) >= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>=(RawType lhs, fit::basic_string_view<CharT, Traits> rhs) noexcept {
  return fit::basic_string_view<CharT, Traits>(lhs).compare(rhs) >= 0;
}

template <class CharT, class Traits, class RawType>
constexpr bool operator>=(fit::basic_string_view<CharT, Traits> lhs, RawType rhs) noexcept {
  return lhs.compare(rhs) >= 0;
}

// Specializations.
using string_view = fit::basic_string_view<char>;

// Constructs a string_view from ""_sv literal.
// Literals with no leading underscore are reserved for the standard library.
// https://en.cppreference.com/w/cpp/string/basic_string_view/operator%22%22sv
inline namespace literals {
inline namespace string_view_literals {

constexpr fit::string_view operator"" _sv(typename fit::string_view::const_pointer str,
                                          typename fit::string_view::size_type len) noexcept {
  return fit::string_view(str, len);
}
}  // namespace string_view_literals
}  // namespace literals

}  // namespace fit

namespace std {
// Hash needs to match basic_string view hash of the same string, so we need to rely on compiler
// implementation.
// https://en.cppreference.com/w/cpp/string/basic_string_view/hash
template <class CharT>
struct hash<fit::basic_string_view<CharT, std::char_traits<CharT>>> {
  std::size_t operator()(const fit::basic_string_view<CharT, std::char_traits<CharT>> val) const {
    return __do_string_hash(val.data(), val.data() + val.length());
  }
};

// Output stream specialization for fit::string_view.
//
// https://en.cppreference.com/w/cpp/string/basic_string_view/operator_ltlt
template <class CharT, class Traits>
std::basic_ostream<CharT, Traits>& operator<<(std::basic_ostream<CharT, Traits>& os,
                                              fit::basic_string_view<CharT, Traits> v) {
  using size_type = typename fit::basic_string_view<CharT, Traits>::size_type;
  const size_type fixed_length = std::min(static_cast<size_type>(os.width()), v.length());
  const size_type fill_length =
      (static_cast<size_type>(os.width()) > v.length()) ? os.width() - v.length() : 0;

  auto fill_space = [](std::basic_ostream<CharT, Traits>& os, size_type fill_length) {
    for (std::size_t i = 0; i < fill_length; ++i) {
      os.put(os.fill());
    }
  };

  bool fill_left = (os.flags() & std::ios_base::adjustfield) == std::ios_base::left;

  if (!fill_left) {
    fill_space(os, fill_length);
  }

  os.write(v.data(), fixed_length);

  if (fill_left) {
    fill_space(os, fill_length);
  }

  os.width(0);

  return os;
}

}  // namespace std

#endif
