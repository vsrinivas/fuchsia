// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdalign.h>
#include <stdint.h>

#include <lib/fidl/cpp/message_part.h>
#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

// Declare placement allocation functions.
// Note: This library does not provide an implementation of these functions.
void* operator new(size_t size, void* ptr);
void* operator new[](size_t size, void* ptr);

namespace fidl {

// Builder helps FIDL clients store decoded objects in a buffer.
//
// Objects are allocated sequentually in the buffer with appropriate alignment
// for in-place encoding. The client is responsible for ordering the objects in
// the buffer appropriately.
class Builder {
public:
    // Creates a buffer without any storage.
    Builder();

    // Creates a buffer that stores objects in the given memory.
    //
    // The constructed |Builder| object does not take ownership of the given
    // storage.
    Builder(void* buffer, uint32_t capacity);
    ~Builder();

    Builder(const Builder& other) = delete;
    Builder& operator=(const Builder& other) = delete;

    // Allocates storage in the buffer of sufficient size to store an object of
    // type |T|. The object must have alignment constraints that are compatible
    // with FIDL messages.
    //
    // If there is insufficient storage in the builder's buffer, this method
    // returns nullptr.
    template <typename T>
    T* New() {
        static_assert(alignof(T) <= FIDL_ALIGNMENT, "");
        static_assert(sizeof(T) <= ZX_CHANNEL_MAX_MSG_BYTES, "");
        if (void* ptr = Allocate(sizeof(T)))
            return new (ptr) T;
        return nullptr;
    }

    // Allocates storage in the buffer of sufficient size to store |count|
    // objects of type |T|. The object must have alignment constraints that are
    // compatible with FIDL messages.
    //
    // If there is insufficient storage in the builder's buffer, this method
    // returns nullptr.
    template <typename T>
    T* NewArray(uint32_t count) {
        static_assert(alignof(T) <= FIDL_ALIGNMENT, "");
        static_assert(sizeof(T) <= ZX_CHANNEL_MAX_MSG_BYTES, "");
        if (sizeof(T) * static_cast<uint64_t>(count) > UINT32_MAX)
            return nullptr;
        if (void* ptr = Allocate(static_cast<uint32_t>(sizeof(T) * count)))
            return new (ptr) T[count];
        return nullptr;
    }

    // Completes the building and returns a |MesssagePart| containing the
    // allocated objects.
    //
    // The allocated objects are placed in the returned buffer in the order in
    // which they were allocated, with appropriate alignment for a FIDL message.
    // The returned buffer's capacity cooresponds to the capacity originally
    // provided to this builder in its constructor.
    BytePart Finalize();

    // Attaches the given storage to the |Builder|.
    //
    // The |Builder| object does not take ownership of the given storage. The
    // next object will be allocated at the start of the buffer.
    void Reset(void* buffer, uint32_t capacity);

protected:
    uint8_t* buffer() const { return buffer_; }
    uint32_t capacity() const { return capacity_; }

private:
    // Returns |size| bytes of zeroed memory aligned to at least FIDL_ALIGNMENT
    void* Allocate(uint32_t size);

    uint32_t capacity_;
    uint32_t at_;
    uint8_t* buffer_;
};

} // namespace fidl
