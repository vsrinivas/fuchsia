// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_FUZZING_TRAITS_H_
#define LIB_FIDL_CPP_FUZZING_TRAITS_H_

#include <lib/fidl/cpp/string.h>
#include <lib/fidl/cpp/vector.h>
#include <lib/fuzzing/cpp/fuzz_input.h>
#include <lib/fuzzing/cpp/traits.h>

#include <cstddef>
#include <string>
#include <vector>

// Note: Namespace must be contained in ::fuzzing to specialize ::fuzzing::MinSize<T> and
// ::fuzzing::Allocate<T>.
namespace fuzzing {

using StringPtr = ::fidl::StringPtr;
template <typename T>
using VectorPtr = ::fidl::VectorPtr<T>;

// Type traits for FIDL-specific types:

// String traits:
// MinSize is 0; take bytes as |const char*| to back |size|-sized string.
template <>
struct MinSize<StringPtr> {
  constexpr operator size_t() const { return 0; }
};
template <>
struct Allocate<StringPtr> {
  StringPtr operator()(FuzzInput* src, size_t* size) {
    if (*size == 0) {
      return StringPtr();
    }

    const char* out = reinterpret_cast<const char*>(src->TakeBytes(*size));
    return std::string(out, *size);
  }
};

// Vector traits:
// MinSize is 0 (i.e., admit empty vector); take MinSize<T>()-byte chunks from
// |src| for constructing instances of T. Allocating larger-than-min-size
// T-instances is currently unsupported.
//
// Caveat: When MinSize<T>() = 0, treat T-instances as though they will
// allocate 8 bytes, enough for a 64-bit pointer.
//
// TODO(fxbug.dev/25053): Consume some input bytes to allocate pseudorandom number of items.
template <typename T>
struct MinSize<VectorPtr<T>> {
  constexpr operator size_t() const { return 0; }
};
template <typename T>
struct Allocate<VectorPtr<T>> {
  VectorPtr<T> operator()(FuzzInput* src, size_t* size) {
    if (*size < kItemSize) {
      *size = 0;
      return VectorPtr<T>();
    }

    return VectorPtr<T>(Allocate<std::vector<T>>{}(src, size));
  }

 private:
  static constexpr size_t kItemSize = MinSize<T>();
};

}  // namespace fuzzing

#endif  // LIB_FIDL_CPP_FUZZING_TRAITS_H_
