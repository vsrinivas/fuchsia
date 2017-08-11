// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <mxtl/alloc_checker.h>
#include <mxtl/atomic.h>
#include <mxtl/string_piece.h>

namespace mxtl {
namespace tests {
struct StringTestHelper;
} // namespace tests

// A string with immutable contents.
//
// mxtl::String is designed to resemble std::string except that its content
// is immutable.  This makes it easy to share string buffers so that copying
// strings does not incur any allocation cost.
//
// Allocation only occurs when initializing or setting a string to a non-empty value.
class String {
public:
    String() { InitWithEmpty(); }

    String(const String& other)
        : data_(other.data_) {
        AcquireRef(data_);
    }

    String(String&& other)
        : data_(other.data_) {
        other.InitWithEmpty();
    }

    String(const char* data) {
        Init(data, constexpr_strlen(data));
    }

    String(const char* data, AllocChecker* ac) {
        Init(data, constexpr_strlen(data), ac);
    }

    String(const char* data, size_t length) {
        Init(data, length);
    }

    String(const char* data, size_t length, AllocChecker* ac) {
        Init(data, length, ac);
    }

    explicit String(const StringPiece& piece)
        : String(piece.data(), piece.length()) {}

    explicit String(const StringPiece& piece, AllocChecker* ac)
        : String(piece.data(), piece.length(), ac) {}

    ~String() { ReleaseRef(data_); }

    const char* data() const { return data_; }
    const char* c_str() const { return data(); }

    size_t length() const { return *length_field_of(data_); }
    size_t size() const { return length(); }
    bool empty() const { return length() == 0u; }

    const char* begin() const { return data(); }
    const char* cbegin() const { return data(); }
    const char* end() const { return data() + length(); }
    const char* cend() const { return data() + length(); }

    const char& operator[](size_t pos) const { return data()[pos]; }

    int compare(const String& other) const;

    void clear();
    void swap(String& other);

    String& operator=(const String& other);
    String& operator=(String&& other);

    String& operator=(const char* data) {
        Set(data);
        return *this;
    }

    String& operator=(const StringPiece& piece) {
        Set(piece);
        return *this;
    }

    void Set(const char* data) {
        Set(data, constexpr_strlen(data));
    }

    void Set(const char* data, AllocChecker* ac) {
        Set(data, constexpr_strlen(data), ac);
    }

    void Set(const char* data, size_t length);
    void Set(const char* data, size_t length, AllocChecker* ac);

    void Set(const StringPiece& piece) {
        Set(piece.data(), piece.length());
    }

    void Set(const StringPiece& piece, AllocChecker* ac) {
        Set(piece.data(), piece.length(), ac);
    }

    // Creates a string piece backed by the string.
    // The string piece does not take ownership of the data so the string
    // must outlast the string piece.
    StringPiece ToStringPiece() const {
        return StringPiece(data(), length());
    }

private:
    friend struct mxtl::tests::StringTestHelper;

    // A string buffer consists of a length followed by a reference count
    // followed by a null-terminated string.  To make access faster, we offset
    // the |data_| pointer to point at the first byte of the content instead of
    // at the beginning of the string buffer itself.
    static constexpr size_t kLengthFieldOffset = 0u;
    static constexpr size_t kRefCountFieldOffset = sizeof(size_t);
    static constexpr size_t kDataFieldOffset = sizeof(size_t) + sizeof(atomic_uint);

    static size_t* length_field_of(char* data) {
        return reinterpret_cast<size_t*>(data - kDataFieldOffset + kLengthFieldOffset);
    }
    static atomic_uint* ref_count_field_of(char* data) {
        return reinterpret_cast<atomic_uint*>(data - kDataFieldOffset + kRefCountFieldOffset);
    }
    static constexpr size_t buffer_size(size_t length) {
        return kDataFieldOffset + length + 1u;
    }

    // For use by test code only.
    unsigned int ref_count() const {
        return ref_count_field_of(data_)->load(memory_order_relaxed);
    }

    // Storage for an empty string.
    struct EmptyBuffer {
        size_t length{0u};
        atomic_uint ref_count{1u};
        char nul{0};
    };
    static_assert(offsetof(EmptyBuffer, length) == kLengthFieldOffset, "");
    static_assert(offsetof(EmptyBuffer, ref_count) == kRefCountFieldOffset, "");
    static_assert(offsetof(EmptyBuffer, nul) == kDataFieldOffset, "");

    static EmptyBuffer gEmpty;

    void Init(const char* data, size_t length);
    void Init(const char* data, size_t length, AllocChecker* ac);
    void InitWithBuffer(void* buffer, const char* data, size_t length);
    void InitWithEmpty();

    static void AcquireRef(char* data);
    static void ReleaseRef(char* data);

    char* data_;
};

bool operator==(const String& lhs, const String& rhs);

inline bool operator!=(const String& lhs, const String& rhs) {
    return !(lhs == rhs);
}

inline bool operator<(const String& lhs, const String& rhs) {
    return lhs.compare(rhs) < 0;
}

inline bool operator>(const String& lhs, const String& rhs) {
    return lhs.compare(rhs) > 0;
}

inline bool operator<=(const String& lhs, const String& rhs) {
    return lhs.compare(rhs) <= 0;
}

inline bool operator>=(const String& lhs, const String& rhs) {
    return lhs.compare(rhs) >= 0;
}

} // namespace mxtl
