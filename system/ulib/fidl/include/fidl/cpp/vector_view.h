// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/fidl.h>

namespace fidl {

template<typename T>
class VectorView : public fidl_vector_t {
public:
    VectorView() : fidl_vector_t{} {}

    uint64_t count() const { return fidl_vector_t::count; }
    void set_count(uint64_t count) { fidl_vector_t::count = count; }

    const T* data() const { return static_cast<T*>(fidl_vector_t::data); }
    void set_data(T* data) { fidl_vector_t::data = data; }

    T* mutable_data() const { return static_cast<T*>(fidl_vector_t::data); }

    bool is_null() const { return fidl_vector_t::data == nullptr; }
    bool empty() const { return fidl_vector_t::count == 0; }

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

    fidl_vector_t* impl() { return this; }
};

} // namespace fidl
