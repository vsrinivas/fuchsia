// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONTAINER_H_
#define SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONTAINER_H_

#include <optional>
#include <string_view>
#include <type_traits>

// This file provides some adapters for container types defined elsewhere to be
// used with the diagnostics.h API for handling allocation failures.  These
// represent the container API expected by other elfldltl template code.
//
// elfldltl template code that needs containers uses a template template
// parameter (template<typename> class Container) to instantiate any
// Container<T> types it needs.  These are used like normal containers, except
// that the methods that can need to allocate (push_back, emplace_back,
// emplace, and insert) take additional Diagnostics& and std::string_view
// parameters first.  For an allocation failure, the Diagnostics object's
// ResourceError or ResourceLimit<N> method will be called with the error
// string (the std::string_view parameter).  The methods that usually return
// void (push_back, emplace_back) instead return bool, with false indicating
// allocation failure.  The methods that usually return an iterator (emplace,
// insert) instead return std::optional<iterator>, with std::nullopt indicating
// allocation failure.

namespace elfldltl {

// elfldltl::StdContainer<C, ...>::Container<T> just uses a standard container
// type C<T, ...>, e.g. elfldltl::StdContainer<std::vector>.
template <template <typename, typename...> class C, typename... P>
struct StdContainer {
  template <typename T>
  class Container : public C<T, P...> {
   public:
    using Base = C<T, P...>;

    using Base::Base;

    template <class Diagnostics, typename U>
    constexpr std::true_type push_back(Diagnostics& diagnostics, std::string_view error,
                                       U&& value) {
      Base::push_back(std::forward<U>(value));
      return {};
    }

    template <class Diagnostics, typename... Args>
    constexpr std::true_type emplace_back(Diagnostics& diagnostics, std::string_view error,
                                          Args&&... args) {
      Base::emplace_back(std::forward<Args>(args)...);
      return {};
    }

    template <class Diagnostics, typename... Args>
    constexpr auto emplace(Diagnostics& diagnostics, std::string_view error, Args&&... args) {
      return std::make_optional(Base::emplace(std::forward<Args>(args)...));
    }

    template <class Diagnostics, typename... Args>
    constexpr auto insert(Diagnostics& diagnostics, std::string_view error, Args&&... args) {
      return std::make_optional(Base::insert(std::forward<Args>(args)...));
    }

   private:
    // Make the original methods unavailable.
    using Base::emplace;
    using Base::emplace_back;
    using Base::insert;
    using Base::push_back;
  };
};

}  // namespace elfldltl

#endif  // SRC_LIB_ELFLDLTL_INCLUDE_LIB_ELFLDLTL_CONTAINER_H_
