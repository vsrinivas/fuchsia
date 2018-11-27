// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_MESSAGE_PART_H_
#define LIB_FIDL_CPP_MESSAGE_PART_H_

#include <stdint.h>
#include <string.h>

#include <zircon/types.h>

namespace fidl {

// Part of a FIDL message.
//
// A FIDL message has two parts: the bytes and the handles. This class is used
// to represent both kinds of parts.
//
// Each part of the message has a data buffer, which contains the actual data
// for that part of the message, a capacity for that buffer, and the actual
// amount of data stored in the buffer, which might be less that the capacity if
// the buffer is not completely full.
template<typename T>
class MessagePart {
public:
    using value_type = T;
    using const_iterator = const T*;

    // A message part with no storage.
    MessagePart() : data_(nullptr), capacity_(0u), actual_(0u) {}

    // A message part that uses the given storage.
    //
    // The constructed |MessagePart| object does not take ownership of the given
    // storage.
    MessagePart(T* data, uint32_t capacity, uint32_t actual = 0u)
        : data_(data), capacity_(capacity), actual_(actual) {}

    MessagePart(const MessagePart& other) = delete;
    MessagePart& operator=(const MessagePart& other) = delete;

    MessagePart(MessagePart&& other)
        : data_(other.data_),
          capacity_(other.capacity_),
          actual_(other.actual_) {
        other.data_ = nullptr;
        other.capacity_ = 0u;
        other.actual_ = 0u;
    }

    MessagePart& operator=(MessagePart&& other) {
        if (this == &other)
            return *this;
        data_ = other.data_;
        capacity_ = other.capacity_;
        actual_ = other.actual_;
        other.data_ = nullptr;
        other.capacity_ = 0u;
        other.actual_ = 0u;
        return *this;
    }

    // The data stored in this part of the message.
    T* data() const { return data_; }

    // The total amount of storage available for this part of the message.
    //
    // This part of the message might not actually use all of this storage. To
    // determine how much storage is actually being used, see |actual()|.
    uint32_t capacity() const { return capacity_; }

    // The amount of storage that is actually being used for this part of the
    // message.
    //
    // There might be more storage available than is actually being used. To
    // determine how much storage is available, see |capacity()|.
    uint32_t actual() const { return actual_; }
    void set_actual(uint32_t actual) { actual_ = actual; }

    T* begin() { return data_; }
    const T* begin() const { return data_; }
    const T* cbegin() const { return data_; }

    T* end() { return data_ + actual_; }
    const T* end() const { return data_ + actual_; }
    const T* cend() const { return data_ + actual_; }

    size_t size() const { return actual_; }

private:
    T* data_;
    uint32_t capacity_;
    uint32_t actual_;
};

using BytePart = MessagePart<uint8_t>;
using HandlePart = MessagePart<zx_handle_t>;

} // namespace fidl

#endif // LIB_FIDL_CPP_MESSAGE_PART_H_
