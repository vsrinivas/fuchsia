// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_CHARS_FROM_H_
#define ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_CHARS_FROM_H_

#include <zircon/assert.h>

#include <cstdint>
#include <iterator>
#include <string_view>

namespace uart {

// Container/view wrapper, turns a container or view of Char-like into a view
// (forward-iterable only) of Char.  Container::value_type is converted to
// OutChar.  If NewlineToCrLf is true, then '\n' elements read as '\r'
// followed by '\n'.
template <typename Container, typename OutChar = uint8_t, bool NewlineToCrLf = true>
class CharsFrom {
 public:
  using value_type = OutChar;

  class iterator {
   public:
    using difference_type = ptrdiff_t;
    using value_type = OutChar;
    using pointer = value_type*;
    using reference = value_type&;
    using iterator_category = std::input_iterator_tag;

    iterator() = default;
    iterator(const iterator&) = default;
    iterator(iterator&&) = default;
    iterator& operator=(const iterator&) = default;
    iterator& operator=(iterator&&) = default;

    template <typename T>
    explicit iterator(T&& it) : it_(std::forward<T>(it)) {}

    bool operator==(const iterator& other) const {
      return other.it_ == it_ && other.pending_lf_ == pending_lf_;
    }

    bool operator!=(const iterator& other) const { return !(*this == other); }

    iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

    iterator& operator++() {  // prefix
      if (NewlineToCrLf && !pending_lf_ && *it_ == '\n') {
        // Advance past the synthesized '\r' but not past the real '\n'.
        pending_lf_ = true;
      } else {
        if constexpr (NewlineToCrLf) {
          ZX_DEBUG_ASSERT(!pending_lf_ || *it_ == '\n');
          pending_lf_ = false;
        }
        ++it_;
      }
      return *this;
    }

    value_type operator*() const {
      value_type c = static_cast<value_type>(*it_);
      return (NewlineToCrLf && !pending_lf_ && c == '\n') ? value_type{'\r'} : c;
    }

   private:
    using Iterator = decltype(std::begin(std::declval<const Container&>()));
    using InChar = std::decay_t<decltype(*std::declval<Iterator>())>;
    static_assert(
        std::is_convertible_v<InChar, value_type>,
        "uart::CharsFrom applied to container with value_type not convertible to uint8_t");

    Iterator it_{};
    bool pending_lf_ = false;
  };

  using const_iterator = iterator;

  explicit CharsFrom(const Container& container) : container_(container) {}

  iterator begin() const { return iterator(std::begin(container_)); }

  iterator end() const { return iterator(std::end(container_)); }

 private:
  const Container& container_;
};

}  // namespace uart

#endif  // ZIRCON_SYSTEM_DEV_LIB_UART_INCLUDE_LIB_UART_CHARS_FROM_H_
