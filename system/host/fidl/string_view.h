// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <string.h>

#include <string>

namespace fidl {

class StringView {
public:
    constexpr StringView()
        : data_(nullptr), size_(0u) {}
    StringView(const StringView& view) = default;
    constexpr StringView(const std::string& string)
        : StringView(string.data(), string.size()) {}
    constexpr StringView(const char* data, size_t size)
        : data_(data), size_(size) {}
    StringView(const char* string)
        : data_(string), size_(strlen(string)) {}

    StringView& operator=(const StringView& view) = default;

    constexpr char operator[](size_t index) const { return data_[index]; }

    constexpr const char* data() const { return data_; }
    constexpr size_t size() const { return size_; }

    bool operator==(StringView other) const {
        if (size() != other.size())
            return false;
        return !memcmp(data(), other.data(), size());
    }

    bool operator!=(StringView other) const {
        return !(*this == other);
    }

    bool operator<(StringView other) const {
        if (size() < other.size())
            return true;
        if (size() > other.size())
            return false;
        return memcmp(data(), other.data(), size()) < 0;
    }

private:
    const char* data_;
    size_t size_;
};

} // namespace fbl
