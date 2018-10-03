// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_STRING_VIEW_H_
#define LIB_FIDL_CPP_STRING_VIEW_H_

#include <zircon/fidl.h>

namespace fidl {

class StringView : public fidl_string_t {
public:
    StringView() : fidl_string_t{} {}

    uint64_t size() const { return fidl_string_t::size; }
    void set_size(uint64_t size) { fidl_string_t::size = size; }

    const char* data() const { return fidl_string_t::data; }
    void set_data(char* data) { fidl_string_t::data = data; }

    char* mutable_data() const { return fidl_string_t::data; }

    bool is_null() const { return fidl_string_t::data == nullptr; }
    bool empty() const { return fidl_string_t::size == 0; }

    const char& at(size_t offset) const { return data()[offset]; }
    char& at(size_t offset) { return mutable_data()[offset]; }

    const char& operator[](size_t offset) const { return at(offset); }
    char& operator[](size_t offset) { return at(offset); }

    char* begin() { return mutable_data(); }
    const char* begin() const { return data(); }
    const char* cbegin() const { return data(); }

    char* end() { return mutable_data() + size(); }
    const char* end() const { return data() + size(); }
    const char* cend() const { return data() + size(); }

    fidl_string_t* impl() { return this; }
};

} // namespace fidl

#endif // LIB_FIDL_CPP_STRING_VIEW_H_
