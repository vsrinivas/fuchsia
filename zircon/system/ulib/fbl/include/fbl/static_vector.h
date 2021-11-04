// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_STATIC_VECTOR_H_
#define FBL_STATIC_VECTOR_H_

#include <zircon/assert.h>

#include <array>
#include <iterator>
#include <type_traits>
#include <utility>

namespace fbl {

namespace internal {

// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0843r4.html#STORAGE
// "If the Capacity is zero the container has zero size."
template <typename T>
class static_vector_storage_empty {
 public:
  constexpr T* data() noexcept { return nullptr; }
  constexpr const T* data() const noexcept { return nullptr; }
  constexpr size_t size() const noexcept { return 0; }
  constexpr void set_size(size_t new_size) noexcept {}

  template <class... Args>
  constexpr void construct_at(size_t k, Args&&... args) {}
  constexpr void destroy_at(size_t k) {}
};

// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0843r4.html#CONSTEXPR
// "If is_trivially_copyable_v<T> && is_default_constructible_v<T> is true, static_vectors
// can be seamlessly used from constexpr code ... This changes the algorithmic complexity of
// static_vector constructors for trivial-types from 'Linear in N' to 'Constant in Capacity'."
//
// Note that TrivallyCopyable implies that T must have a trivial destructor:
// https://en.cppreference.com/w/cpp/named_req/TriviallyCopyable
template <typename T, size_t N>
class static_vector_storage_trivial {
 private:
  size_t size_{0};
  std::array<T, N> raw_data_{};

 public:
  constexpr T* data() noexcept { return &raw_data_[0]; }
  constexpr const T* data() const noexcept { return &raw_data_[0]; }
  constexpr size_t size() const noexcept { return size_; }
  constexpr void set_size(size_t new_size) noexcept { size_ = new_size; }

  // Since this class is used only when is_trivially_copyable_v<T>, it doesn't matter
  // whether or not the old element at k has been destructed because T's destructor
  // is a no-op. This should compile to a memmove.
  template <class... Args>
  constexpr void construct_at(size_t k, Args&&... args) {
    raw_data_[k] = T(std::forward<Args>(args)...);
  }

  // Since T must be trivially destructible, this is a no-op.
  constexpr void destroy_at(size_t k) {}
};

// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0843r4.html#STORAGE
// "The container models ContiguousContainer. The elements of the static_vector are
// contiguously stored and properly aligned within the static_vector object itself.
// The exact location of the contiguous elements within the static_vector is not specified."
template <typename T, size_t N>
class static_vector_storage_nontrivial {
 private:
  size_t size_{0};
  typename std::aligned_storage<sizeof(T), alignof(T)>::type raw_data_[N];

 public:
  T* data() noexcept { return reinterpret_cast<T*>(&raw_data_[0]); }
  const T* data() const noexcept { return reinterpret_cast<const T*>(&raw_data_[0]); }
  size_t size() const noexcept { return size_; }
  void set_size(size_t new_size) noexcept { size_ = new_size; }

  ~static_vector_storage_nontrivial() {
    for (auto p = data(); p < data() + size_; p++) {
      p->~T();
    }
  }

  // Requires that raw_data_[k] is in an unconstructed state.
  template <class... Args>
  void construct_at(size_t k, Args&&... args) {
    new (&raw_data_[k]) T(std::forward<Args>(args)...);
  }

  // Requires that raw_data_[k] is in a constructed state.
  void destroy_at(size_t k) { data()[k].~T(); }
};

template <typename T, size_t N>
struct static_vector_storage {
  using type = std::conditional_t<
      N == 0, internal::static_vector_storage_empty<T>,
      std::conditional_t<std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>,
                         internal::static_vector_storage_trivial<T, N>,
                         internal::static_vector_storage_nontrivial<T, N>>>;
};

template <typename T, size_t N>
using static_vector_storage_t = typename static_vector_storage<T, N>::type;

}  // namespace internal

// This class implements a resizable vector with fixed capacity known at compile time.
// This is a partial implementation of the following proposal:
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2020/p0843r4.html
//
// For now we have elided a few unneeded methods:
//   - swap() method and std::swap() specialization
//   - insert(), emplace(), and erase()
//   - emplace_back()
//   - (in)equality operators
//
template <typename T, size_t N>
class static_vector : private internal::static_vector_storage_t<T, N> {
 private:
  using base_type = typename internal::static_vector_storage_t<T, N>;

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

 public:
  //
  // Construction
  //

  constexpr static_vector() noexcept = default;
  constexpr static_vector(const static_vector& rhs) { *this = rhs; }
  constexpr static_vector(static_vector&& rhs) { *this = std::move(rhs); }

  // Construct a vector of the given size with n default-constructed elements.
  // Requires n <= N.
  constexpr explicit static_vector(size_type n) { resize(n); }

  // Construct a vector of the given size with n copies of value.
  // Requires n <= N.
  constexpr static_vector(size_type n, const value_type& value) { resize(n, value); }

  // Construct a vector as a copy of the range [first, last]
  // Requires std::distance(first, last) <= N.
  template <class InputIterator>
  constexpr static_vector(InputIterator first, InputIterator last) {
    assign(first, last);
  }

  // Construct a vector as a copy of the initializer list.
  // Requires init.size() <= N.
  constexpr static_vector(std::initializer_list<value_type> init) { assign(init); }

  ~static_vector() = default;

  //
  // Assignment
  //

  constexpr static_vector& operator=(const static_vector& rhs) {
    clear();
    set_size(rhs.size());
    for (size_type k = 0; k < size(); k++) {
      construct_at(k, rhs[k]);
    }
    return *this;
  }

  constexpr static_vector& operator=(static_vector&& rhs) {
    clear();
    set_size(rhs.size());
    for (size_type k = 0; k < size(); k++) {
      construct_at(k, std::move(rhs[k]));
    }
    return *this;
  }

  template <class InputIterator>
  constexpr void assign(InputIterator first, InputIterator last) {
    clear();
    size_type k = 0;
    for (; first != last; k++, first++) {
      ZX_DEBUG_ASSERT(k < N);
      construct_at(k, *first);
    }
    set_size(k);
  }

  constexpr void assign(size_type n, const value_type& value) {
    clear();
    resize(n, value);
  }

  constexpr void assign(std::initializer_list<value_type> init) {
    clear();
    ZX_DEBUG_ASSERT(init.size() <= N);
    size_type k = 0;
    for (const auto& v : init) {
      construct_at(k, v);
      k++;
    }
    set_size(k);
  }

  //
  // Iterators
  //

  // clang-format off

  constexpr iterator               begin()         noexcept { return data(); }
  constexpr const_iterator         begin()   const noexcept { return data(); }
  constexpr iterator               end()           noexcept { return data() + size(); }
  constexpr const_iterator         end()     const noexcept { return data() + size(); }
  constexpr reverse_iterator       rbegin()        noexcept { return reverse_iterator(end()); }
  constexpr const_reverse_iterator rbegin()  const noexcept { return const_reverse_iterator(end()); }
  constexpr reverse_iterator       rend()          noexcept { return reverse_iterator(begin()); }
  constexpr const_reverse_iterator rend()    const noexcept { return const_reverse_iterator(begin()); }
  constexpr const_iterator         cbegin()  const noexcept { return begin(); }
  constexpr const_iterator         cend()    const noexcept { return end(); }
  constexpr const_reverse_iterator crbegin() const noexcept { return rbegin(); }
  constexpr const_reverse_iterator crend()   const noexcept { return rend(); }

  // clang-format on

  //
  // Size and capacity
  //

  // Returns true if the vector is currently empty;
  constexpr bool empty() const noexcept { return size() == 0; }

  // Returns the current size of the vector.
  using base_type::size;

  // Returns the maximum possible size of the vector.
  static constexpr size_type max_size() noexcept { return N; }
  static constexpr size_type capacity() noexcept { return N; }

  // Resize the vector to the give size. If the vector shrinks, the erased items
  // are destructed. If the vector grows, the new items are default constructed.
  constexpr void resize(size_type new_size) {
    ZX_DEBUG_ASSERT(new_size <= N);

    if (new_size < size()) {
      destroy_range(new_size, size());
    } else {
      construct_range(size(), new_size);
    }

    set_size(new_size);
  }

  // Resize the vector to the give size. If the vector shrinks, the erased items
  // are destructed. If the vector grows, the new items are assigned a copy of the
  // given value.
  constexpr void resize(size_type new_size, const value_type& value) {
    ZX_DEBUG_ASSERT(new_size <= N);

    if (new_size < size()) {
      destroy_range(new_size, size());
    } else {
      construct_range(size(), new_size, value);
    }

    set_size(new_size);
  }

  //
  // Element access
  //

  // clang-format off

  constexpr reference       operator[](size_type n)       { return data()[n]; }
  constexpr const_reference operator[](size_type n) const { return data()[n]; }

  constexpr reference       front()       { return data()[0]; }
  constexpr const_reference front() const { return data()[0]; }
  constexpr reference       back()        { return data()[size() - 1]; }
  constexpr const_reference back()  const { return data()[size() - 1]; }

  // clang-format on

  using base_type::data;

  //
  // Modifiers
  //

  constexpr void push_back(const value_type& value) {
    ZX_DEBUG_ASSERT(size() < N);
    construct_at(size(), value);
    set_size(size() + 1);
  }

  constexpr void push_back(value_type&& value) {
    ZX_DEBUG_ASSERT(size() < N);
    construct_at(size(), std::move(value));
    set_size(size() + 1);
  }

  constexpr void pop_back() {
    ZX_DEBUG_ASSERT(size() > 0);
    resize(size() - 1);
  }

  constexpr void clear() noexcept { resize(0); }

 private:
  using base_type::construct_at;
  using base_type::destroy_at;
  using base_type::set_size;

  template <class... Args>
  constexpr void construct_range(size_type first, size_type last, Args&&... args) {
    ZX_DEBUG_ASSERT(last <= N);
    for (; first < last; first++) {
      construct_at(first, std::forward<Args>(args)...);
    }
  }

  constexpr void destroy_range(size_type first, size_type last) {
    ZX_DEBUG_ASSERT(last <= N);
    for (; first < last; first++) {
      destroy_at(first);
    }
  }
};

}  // namespace fbl

#endif  // FBL_STATIC_VECTOR_H_
