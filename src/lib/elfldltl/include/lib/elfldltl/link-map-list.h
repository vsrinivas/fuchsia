// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "layout.h"

namespace elfldltl {

template <class Elf, class Memory, typename T = typename Elf::LinkMap>
class LinkMapList {
 public:
  using value_type = T;

  class iterator {
   public:
    constexpr iterator() = default;
    constexpr iterator(const iterator&) = default;

    constexpr bool operator==(const iterator& other) const { return address_ == other.address_; }

    constexpr bool operator!=(const iterator& other) const { return !(*this == other); }

    constexpr const value_type& operator*() const { return *value_; }

    constexpr iterator& operator++() {  // prefix
      address_ = reinterpret_cast<const typename Elf::LinkMap*>(value_)->next;
      Update();
    }

    constexpr iterator operator++(int) {  // postfix
      iterator old = *this;
      ++*this;
      return old;
    }

   private:
    friend LinkMapList;

    constexpr iterator(Memory& memory, typename Elf::size_type address)
        : memory_(&memory), address_(address) {
      Update();
    }

    // Read the struct from the current address pointer into value_.
    // If the pointer can't be read, reset address_ to zero (end state).
    constexpr void Update() {
      if (address_ != 0) {
        if (auto data = memory_->template ReadArray<value_type>(address_, 1)) {
          value_ = data->data();
        } else {
          value_ = nullptr;
          address_ = 0;
        }
      }
    }

    Memory* memory_ = nullptr;
    const value_type* value_ = nullptr;
    typename Elf::size_type address_ = 0;
  };

  using const_iterator = iterator;

  constexpr LinkMapList(const LinkMapList&) = default;

  constexpr LinkMapList(Memory& memory, typename Elf::size_type map) : memory_(memory), map_(map) {}

  iterator begin() const { return iterator(memory_, map_); }

  iterator end() const { return iterator(memory_, 0); }

 private:
  Memory& memory_;
  typename Elf::size_type map_;
};

}  // namespace elfldltl
