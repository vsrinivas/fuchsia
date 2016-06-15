// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <string.h>

namespace utils {

// A string-like object that points to a sized piece of memory.
// The string data may or may not be null terminated.
// The string piece does not own the data it points to.

class StringPiece {
public:
    constexpr StringPiece() : ptr_(nullptr), length_(0) {}
    StringPiece(const char* str) : ptr_(str), length_((str == nullptr) ? 0 : strlen(str)) {}
    constexpr StringPiece(const char* str, size_t len) : ptr_(str), length_(len) {}

    const char* data() const {
        return ptr_;
    }
    size_t length() const {
        return length_;
    }

    void set(const char* data_in, size_t len) {
        ptr_ = data_in;
        length_ = len;
    }

private:
    // Pointer to string data, not necessarily null terminated
    const char* ptr_;
    // Length of the string data
    size_t length_;
};

}  // namespace utils
