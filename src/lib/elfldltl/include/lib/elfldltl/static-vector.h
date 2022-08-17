// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_

#include <lib/stdcompat/span.h>

#include <new>
#include <optional>
#include <string_view>
#include <type_traits>

namespace elfldltl {

// elfldltl::StaticVector<N>::Container can be used with elfldltl::LoadInfo.
//
// This wraps a fixed T[N] array with a container interface that looks like a
// std::vector but provides the container.h API for the allocating methods.
template <size_t N>
struct StaticVector {
  template <typename T>
  class Container {
   public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = typename cpp20::span<T, N>::iterator;
    using const_iterator = typename cpp20::span<const T, N>::iterator;
    using reverse_iterator = typename cpp20::span<T, N>::reverse_iterator;
    using const_reverse_iterator = typename cpp20::span<const T, N>::reverse_iterator;

    // All the basic std::vector methods that don't need to allocate are here.

    static constexpr size_t max_size() { return N; }

    static constexpr size_t capacity() { return N; }

    constexpr cpp20::span<T> as_span() {
      return cpp20::span(reinterpret_cast<T*>(storage_.array_), capacity()).subspan(0, size_);
    }
    constexpr cpp20::span<const T> as_span() const {
      return cpp20::span(reinterpret_cast<const T*>(storage_.array_), capacity()).subspan(0, size_);
    }

    constexpr pointer data() { return as_span().data(); }
    constexpr const_pointer data() const { return as_span().data(); }

    constexpr size_t size() const { return as_span().size(); }

    constexpr bool empty() const { return as_span().empty(); }

    constexpr iterator begin() { return as_span().begin(); }
    constexpr iterator end() { return as_span().end(); }

    constexpr const_iterator begin() const { return as_span().begin(); }
    constexpr const_iterator end() const { return as_span().end(); }

    constexpr const_iterator cbegin() const { return as_span().begin(); }
    constexpr const_iterator cend() const { return as_span().end(); }

    constexpr reverse_iterator rbegin() { return as_span().rbegin(); }
    constexpr reverse_iterator rend() { return as_span().rend(); }

    constexpr const_reverse_iterator rbegin() const { return crbegin(); }
    constexpr const_reverse_iterator rend() const { return crend(); }

    constexpr const_reverse_iterator crbegin() const { return as_span().rbegin(); }
    constexpr const_reverse_iterator crend() const { return as_span().rend(); }

    constexpr reference at(size_t pos) { return as_span()[pos]; }
    constexpr const_reference at(size_t pos) const { return as_span()[pos]; }

    constexpr reference operator[](size_t pos) { return at(pos); }
    constexpr const_reference operator[](size_t pos) const { return at(pos); }

    constexpr reference front() { return *begin(); }
    constexpr const_reference front() const { return *begin(); }

    constexpr reference back() { return *std::prev(end()); }
    constexpr const_reference back() const { return *std::prev(end()); }

    constexpr void pop_back() {
      assert(size_ > 0);
      back().~T();
      --size_;
    }

    constexpr void clear() {
      while (!empty()) {
        pop_back();
      }
    }

    constexpr iterator erase(iterator pos) { return erase(pos, pos); }

    constexpr iterator erase(const_iterator pos) { return erase(pos, pos); }

    constexpr iterator erase(iterator first, iterator last) {
      iterator out = first;
      for (iterator in = std::next(last); in != end(); ++in) {
        *out++ = std::move(*in);
      }
      while (out != end()) {
        pop_back();
      }
      return first;
    }

    constexpr iterator erase(const_iterator first, const_iterator last) {
      return erase(begin() + (first - cbegin()), begin() + (last - cbegin()));
    }

    constexpr void resize(size_t new_size) {
      assert(new_size <= size_);
      while (new_size < size_) {
        pop_back();
      }
    }

    // The cases that require allocation are only supported via the methods
    // that use the diagnostics.h API to report failures.

    template <class Diagnostics>
    constexpr bool resize(Diagnostics& diagnostics, std::string_view error, size_t new_size) {
      if (new_size > N) [[unlikely]] {
        diagnostics.template ResourceLimit<max_size()>(error, new_size);
        return false;
      }
      if (new_size < size_) {
        resize(new_size);
        return true;
      }

      // Default-construct the new elements.
      size_t old_size = size_;
      size_ = new_size;
      for (auto& elt : cpp20::span(data(), capacity()).subspan(old_size)) {
        new (&elt) T();
      }
      return true;
    }

    template <class Diagnostics>
    constexpr bool push_back(Diagnostics& diagnostics, std::string_view error, const T& elt) {
      return emplace_back(diagnostics, error, elt);
    }

    template <class Diagnostics>
    constexpr bool push_back(Diagnostics& diagnostics, std::string_view error, T&& elt) {
      return emplace_back(diagnostics, error, std::move(elt));
    }

    template <class Diagnostics, typename... Args>
    constexpr bool emplace_back(Diagnostics& diagnostics, std::string_view error, Args&&... args) {
      if (size_ >= max_size()) [[unlikely]] {
        diagnostics.template ResourceLimit<max_size()>(error);
        return false;
      }
      new (std::addressof(data()[size_++])) T{std::forward<Args>(args)...};
      return true;
    }

    template <class Diagnostics, typename... Args>
    constexpr std::optional<iterator> emplace(Diagnostics& diagnostics, std::string_view error,
                                              const_iterator it, Args&&... args) {
      if (size_ >= max_size()) [[unlikely]] {
        diagnostics.template ResourceLimit<max_size()>(error);
        return std::nullopt;
      }

      // The new element will go where the existing elements are now.
      iterator write = MoveElements(it, 1);

      // Construct the new element.
      new (std::addressof(*write)) T{std::forward<Args>(args)...};

      // Return the iterator to where it was written.
      return write;
    }

    template <class Diagnostics>
    constexpr std::optional<iterator> insert(Diagnostics& diagnostics, std::string_view error,
                                             const_iterator it, const T& value) {
      return emplace(diagnostics, error, it, value);
    }

    template <class Diagnostics>
    constexpr std::optional<iterator> insert(Diagnostics& diagnostics, std::string_view error,
                                             const_iterator it, T&& value) {
      return emplace(diagnostics, error, it, std::move(value));
    }

    template <class Diagnostics, typename InputIt>
    constexpr std::optional<iterator> insert(Diagnostics& diagnostics, std::string_view error,
                                             const_iterator it, InputIt first, InputIt last) {
      const size_t count = std::distance(first, last);
      if (max_size() - size_ < count) [[unlikely]] {
        diagnostics.template ResourceLimit<max_size()>(error, size_ + count);
        return std::nullopt;
      }

      // The new elements will go where the existing elements are now.
      iterator write = MoveElements(it, count);
      iterator ret = write;

      // Copy/move-construct the new elements.
      while (first != last) {
        new (std::addressof(*write++)) T{*first++};
      }

      // Return the iterator to the beginning where they were written.
      return ret;
    }

    constexpr Container() {}

    template <class Diagnostics>
    constexpr Container(Diagnostics& diagnostics, std::string_view error,
                        std::initializer_list<T> list) {
      insert(diagnostics, error, begin(), list.begin(), list.end());
    }

    ~Container() { clear(); }

   private:
    // Using a union allows leaving the array elements uninitialized even if
    // the type has a nontrivial default constructor.  The first field is the
    // default one for initialization, and it's uninitialized.  So the T
    // constructor is not called implicitly by the StaticVector constructor.
    // Instead, it's only called via placement new in the insertion methods.
    union Storage {
      std::aligned_union_t<sizeof(T[N]), T[N]> uninitialized_;
      T array_[N];

      Storage() {}
      ~Storage() {}
    };

    // Return it+count after increasing size by count and moving all the
    // elements at [it, it+count) up.
    constexpr iterator MoveElements(const_iterator it, size_t count) {
      iterator ret = begin() + (it - cbegin());
      for (iterator write = (end() - 1 + count); write - count >= ret; write--) {
        *write = std::move(*(write - count));
      }

      // Now advance end() to include the new slots.
      size_ += count;

      return ret;
    }

    size_t size_ = 0;
    Storage storage_;
  };
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_
