// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_WORD_VIEW_H_
#define ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_WORD_VIEW_H_

#include <zircon/assert.h>

#include <string_view>

// This provides a container-like view of string_view for each
// whitespace-separated word in the overall line.
class WordView {
 public:
  class iterator {
   public:
    iterator() = default;
    iterator(const iterator&) = default;
    iterator& operator=(const iterator&) = default;
    ~iterator() { Check(); }

    bool operator==(const iterator& other) const { return rest_.data() == other.rest_.data(); }
    bool operator!=(const iterator& other) const { return !(*this == other); }

    // Note that ++end() just yields end(), which begin() relies on (below).
    iterator& operator++();     // prefix
    iterator operator++(int) {  // postfix
      auto old = *this;
      ++*this;
      return old;
    }

    std::string_view operator*() const { return word_; }

   private:
    // This is called only by begin() and end(), below.
    friend WordView;
    iterator(std::string_view line, std::string_view word) : rest_(line), word_(word) {}

    // Check invariants: Current word_ is a subset of the rest_.
    void Check() const {
      ZX_DEBUG_ASSERT(word_.begin() >= rest_.begin());
      ZX_DEBUG_ASSERT(word_.end() <= rest_.end());
    }

    // rest_ includes the current word_.
    std::string_view rest_, word_;
  };

  using const_iterator = iterator;

  WordView() = delete;
  explicit WordView(std::string_view line) : line_(line) {}

  iterator begin() const { return ++iterator{line_, line_.substr(0, 0)}; }

  iterator end() const {
    auto limit = line_.substr(line_.size());
    return iterator{limit, limit};
  }

  const_iterator cbegin() const { return begin(); }
  const_iterator cend() const { return end(); }

 private:
  std::string_view line_;
};

#endif  // ZIRCON_KERNEL_LIB_BOOT_OPTIONS_INCLUDE_LIB_BOOT_OPTIONS_WORD_VIEW_H_
