// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_CONTAINERS_CPP_ARRAY_VIEW_H_
#define SRC_LIB_CONTAINERS_CPP_ARRAY_VIEW_H_

#include <cstddef>
#include <iterator>
#include <vector>

#include "src/lib/containers/cpp/ownership.h"

namespace containers {

// This is like a std::string_view but for array data. It attempts to have the same API as
// std::vector without owning the underlying buffer.
//
// The recommended way to pass an array_view to a function is by value. It consists of two pointer
// which is normally more efficient to push on the stack directly than to push one pointer that
// points to two other pointers.
template <typename T>
class __POINTER(T) array_view {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = const T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr size_t npos = static_cast<size_t>(-1);

  array_view() : begin_(nullptr), end_(nullptr) {}

  array_view(const T* begin, const T* end) : begin_(begin), end_(end) {}
  array_view(const T* begin, size_t size) : begin_(begin), end_(begin + size) {}
  array_view(const std::vector<T>& vect)
      : begin_(vect.empty() ? nullptr : &vect[0]),
        end_(vect.empty() ? nullptr : &vect[0] + vect.size()) {}

  const T& operator[](size_t i) const { return begin_[i]; }
  const T* data() const { return begin_; }

  const T& front() const { return *begin_; };
  const T& back() const { return *(end_ - 1); }

  const T* begin() const { return begin_; }
  const T* end() const { return end_; }
  const_reverse_iterator rbegin() const { return std::reverse_iterator(end()); }
  const_reverse_iterator rend() const { return std::reverse_iterator(begin()); }

  const T* cbegin() const { return begin_; }
  const T* cend() const { return end_; }
  const_reverse_iterator crbegin() const { return std::reverse_iterator(end()); }
  const_reverse_iterator crend() const { return std::reverse_iterator(begin()); }

  bool empty() const { return begin_ == end_; }
  size_t size() const { return end_ - begin_; }

  // subview() has the same variants and behaves the same as std::string::substr().
  array_view<T> subview(size_t pos = 0, size_t count = npos) const {
    const T* new_begin;
    const T* new_end;
    if (pos < size()) {
      new_begin = &begin_[pos];
      if (count == npos || pos + count > size())
        new_end = end_;
      else
        new_end = &begin_[pos + count];
    } else {
      new_begin = nullptr;
      new_end = nullptr;
    }

    return array_view<T>(new_begin, new_end);
  }

 private:
  const T* begin_ = nullptr;
  const T* end_ = nullptr;
};

}  // namespace containers

#endif  // SRC_LIB_CONTAINERS_CPP_ARRAY_VIEW_H_
