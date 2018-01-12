// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdalign.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <fidl/types.h>

// Declare placement allocation functions.
// Note: This library does not provide an implementation of these functions.
void* operator new(size_t size, void* ptr);
void* operator new[](size_t size, void* ptr);

namespace fidl {

class Builder {
public:
    Builder(void* buffer, size_t capacity);
    ~Builder();

    Builder(const Builder& other) = delete;
    Builder& operator=(const Builder& other) = delete;

    template <typename T>
    T* New() {
        static_assert(alignof(T) <= FIDL_ALIGNMENT, "");
        if (void* ptr = Allocate(sizeof(T)))
            return new (ptr) T;
        return nullptr;
    }

    template <typename T>
    T* NewArray(size_t count) {
        static_assert(alignof(T) <= FIDL_ALIGNMENT, "");
        if (void* ptr = Allocate(sizeof(T) * count))
            return new (ptr) T[count];
        return nullptr;
    }

private:
    // Returns |size| bytes of zeroed memory aligned to at least FIDL_ALIGNMENT
    void* Allocate(size_t size);

    const size_t capacity_;
    size_t at_;
    uint8_t* const buffer_;
};

} // namespace fidl
