// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string.h>

namespace mxtl {

// A string-like object that points to a sized piece of memory.
// |length_| does NOT include a trailing NUL and no guarantee is made that
// you can check |ptr_[length_]| to see if a NUL is there.
// Basically, these aren't C strings, don't think otherwise.
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

    int compare(StringPiece other) const;

private:
    // Pointer to string data, not necessarily null terminated
    const char* ptr_;
    // Length of the string data
    size_t length_;
};

bool operator==(StringPiece lhs, StringPiece rhs);
bool operator!=(StringPiece lhs, StringPiece rhs);

inline bool operator<(StringPiece lhs, StringPiece rhs) {
  return lhs.compare(rhs) < 0;
}

inline bool operator>(StringPiece lhs, StringPiece rhs) {
  return lhs.compare(rhs) > 0;
}

inline bool operator<=(StringPiece lhs, StringPiece rhs) {
  return lhs.compare(rhs) <= 0;
}

inline bool operator>=(StringPiece lhs, StringPiece rhs) {
  return lhs.compare(rhs) >= 0;
}

}  // namespace mxtl
