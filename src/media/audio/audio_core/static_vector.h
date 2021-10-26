// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_STATIC_VECTOR_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_STATIC_VECTOR_H_

#include <zircon/assert.h>

#include <iterator>
#include <type_traits>
#include <utility>

namespace media::audio {

// This class implements a resizable vector with fixed capacity known at compile time.
// This is a partial implementation of the following proposal:
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0843r4.html
//
// For now we have elided a few unneeded methods:
//   - max_size(), which is redundant with capacity()
//   - assign()
//   - swap() method and std::swap() specialization
//   - insert()
//   - emplace()
//   - emplace_back()
//   - erase()
//   - (in)equality operators
//
template <typename T, size_t N>
class static_vector {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
  using iterator = T*;
  using const_iterator = const T*;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

 private:
  size_t size_ = 0;
  typename std::aligned_storage<sizeof(T), alignof(T)>::type data_[N];

 public:
  //
  // Construction
  //

  constexpr static_vector() = default;

  constexpr static_vector(const static_vector& rhs) : size_(rhs.size_) {
    for (size_type k = 0; k < size_; k++) {
      new (&data_[k]) T(rhs[k]);
    }
  }

  constexpr static_vector(static_vector&& rhs) : size_(rhs.size_) {
    for (size_type k = 0; k < size_; k++) {
      new (&data_[k]) T(std::move(rhs[k]));
    }
    rhs.size_ = 0;
  }

  // Construct a vector of the given size with n default-constructed elements.
  // Requires n <= N.
  constexpr explicit static_vector(size_type n) : size_(n) {
    ZX_DEBUG_ASSERT(n <= N);
    for (size_type k = 0; k < n; k++) {
      new (&data_[k]) T();
    }
  }

  // Construct a vector of the given size with n copies of value.
  // Requires n <= N.
  constexpr static_vector(size_type n, const value_type& value) : size_(n) {
    ZX_DEBUG_ASSERT(n <= N);
    for (size_type k = 0; k < n; k++) {
      new (&data_[k]) T(value);
    }
  }

  // Construct a vector as a copy of the range [first, last]
  // Requires std::distance(first, last) <= N.
  template <class InputIterator>
  constexpr static_vector(InputIterator first, InputIterator last) {
    for (size_type k = 0; first != last; k++, first++) {
      ZX_DEBUG_ASSERT(k < N);
      new (&data_[k]) T(*first);
    }
  }

  // Construct a vector as a copy of the initializer list.
  // Requires init.size() <= N.
  constexpr static_vector(std::initializer_list<value_type> init) {
    size_type k = 0;
    for (auto& v : init) {
      new (&data_[k]) T(v);
      k++;
    }
  }

  ~static_vector() { clear(); }

  //
  // Assignment
  //

  constexpr static_vector& operator=(const static_vector& other) = default;
  constexpr static_vector& operator=(static_vector&& other) = default;

  //
  // Iterators
  //

  constexpr iterator begin() { return data(); }
  constexpr const_iterator begin() const { return data(); }
  constexpr iterator end() { return data() + size_; }
  constexpr const_iterator end() const { return data() + size_; }
  constexpr reverse_iterator rbegin() { return reverse_iterator(end()); }
  constexpr const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
  constexpr reverse_iterator rend() { return reverse_iterator(begin()); }
  constexpr const_reverse_iterator rend() const { return reverse_iterator(begin()); }
  constexpr const_iterator cbegin() const { return begin(); }
  constexpr const_iterator cend() const { return end(); }
  constexpr const_reverse_iterator crbegin() const { return rbegin(); }
  constexpr const_reverse_iterator crend() const { return rend(); }

  //
  // Size and capacity
  //

  // Returns true if the vector is currently empty;
  constexpr bool empty() const { return size_ == 0; }

  // Returns the current size of the vector.
  constexpr size_type size() const { return size_; }

  // Returns the maximum possible size of the vector.
  static constexpr size_type capacity() { return N; }

  // Resize the vector to the give size. If the vector shrinks, the erased items
  // are destructed. If the vector grows, the new items are default constructed.
  constexpr void resize(size_type sz) {
    ZX_DEBUG_ASSERT(sz <= N);

    if (sz < size_) {
      for (size_type k = sz; k < size_; k++) {
        (*this)[k].~T();
      }
    } else {
      for (size_type k = size_; k < sz; k++) {
        new (&data_[k]) T();
      }
    }

    size_ = sz;
  }

  // Resize the vector to the give size. If the vector shrinks, the erased items
  // are destructed. If the vector grows, the new items are assigned a copy of the
  // given value.
  constexpr void resize(size_type sz, const value_type& value) {
    ZX_DEBUG_ASSERT(sz <= N);

    if (sz < size_) {
      for (size_type k = sz; k < size_; k++) {
        (*this)[k].~T();
      }
    } else {
      for (size_type k = size_; k < sz; k++) {
        new (&data_[k]) T(value);
      }
    }

    size_ = sz;
  }

  //
  // Element access
  //

  constexpr reference operator[](size_type n) { return data()[n]; }
  constexpr const_reference operator[](size_type n) const { return data()[n]; }
  constexpr reference front() { return data()[0]; }
  constexpr const_reference front() const { return data()[0]; }
  constexpr reference back() { return data()[size_ - 1]; }
  constexpr const_reference back() const { return data()[size_ - 1]; }
  constexpr T* data() { return reinterpret_cast<T*>(&data_[0]); }
  constexpr const T* data() const { return reinterpret_cast<const T*>(&data_[0]); }

  //
  // Modifiers
  //

  constexpr void push_back(const value_type& value) {
    ZX_DEBUG_ASSERT(size_ < N);
    new (&data_[size_]) T(value);
    size_++;
  }

  constexpr void push_back(value_type&& value) {
    ZX_DEBUG_ASSERT(size_ < N);
    new (&data_[size_]) T(std::move(value));
    size_++;
  }

  constexpr void pop_back() {
    ZX_DEBUG_ASSERT(size_ > 0);
    (*this)[size_ - 1].~T();
  }

  constexpr void clear() {
    for (size_type k = 0; k < size_; k++) {
      (*this)[k].~T();
    }
  }
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_STATIC_VECTOR_H_
