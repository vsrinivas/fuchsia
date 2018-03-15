// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>

namespace fidl {

template<typename T, size_t N>
class Array {
public:
    constexpr size_t count() const { return N; }

    const T* data() const { return data_; }
    T* mutable_data() { return data_; }

    const T& at(size_t offset) const { return data()[offset]; }
    T& at(size_t offset) { return mutable_data()[offset]; }

    const T& operator[](size_t offset) const { return at(offset); }
    T& operator[](size_t offset) { return at(offset); }

    T* begin() { return mutable_data(); }
    const T* begin() const { return data(); }
    const T* cbegin() const { return data(); }

    T* end() { return mutable_data() + count(); }
    const T* end() const { return data() + count(); }
    const T* cend() const { return data() + count(); }

private:
    static_assert(N > 0, "fid::Array cannot have zero elements.");

    T data_[N];
};

} // namespace fidl
