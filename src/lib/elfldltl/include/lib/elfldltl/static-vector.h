// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_

#include <lib/stdcompat/span.h>

#include <array>

#include "preallocated-vector.h"

namespace elfldltl {

// elfldltl::StaticVector<N>::Container can be used with elfldltl::LoadInfo.
//
// This wraps a fixed T[N] array with a container interface that looks like a
// std::vector but provides the container.h API for the allocating methods.
template <size_t N>
struct StaticVector {
  template <typename T>
  class Container : public PreallocatedVector<T, N> {
   public:
    using typename PreallocatedVector<T, N>::iterator;
    using typename PreallocatedVector<T, N>::const_iterator;

    constexpr Container() : PreallocatedVector<T, N>(cpp20::span<T, N>{storage_.array_}) {}

    template <class Diagnostics>
    constexpr Container(Diagnostics& diagnostics, std::string_view error,
                        std::initializer_list<T> list)
        : Container() {
      this->insert(diagnostics, error, this->begin(), list.begin(), list.end());
    }

   private:
    // Using a union allows leaving the array elements uninitialized even if
    // the type has a nontrivial default constructor.  The first field is the
    // default one for initialization, and it's uninitialized.  So the T
    // constructor is not called implicitly by the StaticVector constructor.
    // Instead, it's only called via placement new in the insertion methods.
    union Storage {
      Storage() {}

      ~Storage() {}

      struct {
      } uninitialized_;

      std::array<T, N> array_;
    };

    Storage storage_;
  };
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_STATIC_VECTOR_H_
