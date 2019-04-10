// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_ARRAY_H_
#define LIB_FIDL_LLCPP_ARRAY_H_

#include <zircon/fidl.h>

namespace fidl {

// Implementation of std::array guaranteed to have the same memory layout as a C array,
// hence the same layout as the FIDL wire-format.
// The standard does not guarantee that there are no trailing padding bytes in std::array.
// When adding new functionalities to this struct, the data layout should not be changed.
template <typename T, size_t N>
struct Array final {
    static constexpr size_t size() { return N; }

    const T* data() const { return data_; }
    T* data() { return data_; }

    const T& at(size_t offset) const { return data()[offset]; }
    T& at(size_t offset) { return data()[offset]; }

    const T& operator[](size_t offset) const { return at(offset); }
    T& operator[](size_t offset) { return at(offset); }

    T* begin() { return data(); }
    const T* begin() const { return data(); }
    const T* cbegin() const { return data(); }

    T* end() { return data() + size(); }
    const T* end() const { return data() + size(); }
    const T* cend() const { return data() + size(); }

    // Keeping data_ public such that an aggregate initializer can be used.
    T data_[N];

    static_assert(N > 0, "fidl::Array cannot have zero elements.");
};

} // namespace fidl

#endif // LIB_FIDL_LLCPP_ARRAY_H_
