// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

// |crypto::Bytes| is a small helper class that simply wraps a buffer.  It saves on some boilerplate
// when allocating a buffer.  More importantly, when going out of scope, the destructor guarantees
// that the buffer will be zeroed in a way that will not be optimized away.  Any buffer that holds
// cryptographically sensitive random data should be a |Bytes| and get its data via a call to
// |Bytes::Randomize|.
namespace crypto {

class Bytes final {
public:
    Bytes();
    ~Bytes();

    // Accessors
    const uint8_t* get() const { return buf_.get(); }
    uint8_t* get() { return buf_.get(); }
    size_t len() const { return len_; }

    // Resets self and then takes ownership of the given |buf| of |len| bytes.
    void Adopt(fbl::unique_ptr<uint8_t[]> buf, size_t len);

    // Discards the current contents and allocates a new buffer of |size| bytes initialized zero.
    zx_status_t InitZero(size_t size);

    // Discards the current contents and allocates a new buffer of |size| bytes initialized with a
    // pseudorandom bytes sequence.
    zx_status_t InitRandom(size_t size);

    // Fills the underlying buffer with the given |value|
    zx_status_t Fill(uint8_t value);

    // Fills the underlying buffer with random data.
    zx_status_t Randomize();

    // Resize the underlying buffer.  If the new length is shorter, the data is truncated.  If it is
    // longer, it is padded with the given |fill| value.
    zx_status_t Resize(size_t size, uint8_t fill = 0);

    // Copies |len| bytes from |src| to |dst_off| in the underlying buffer.  Resizes the buffer as
    // needed, padding with zeros.
    zx_status_t Copy(const void* src, size_t len, zx_off_t dst_off = 0);

    // Copies |src|'s data to |dst_off| in the underlying buffer.  Resizes the buffer as needed,
    // padding with zeros.
    zx_status_t Copy(const Bytes& src, zx_off_t dst_off = 0);

    // Treats the contents of this object as a (big-endian) arbitrary precision unsigned integer and
    // increments it by the given |amount|.  Returns |ZX_ERR_OUT_OF_RANGE| if incrementing would
    // overflow.
    zx_status_t Increment(uint64_t amount = 1);

    // Copies the contents of |tail| to the end of this buffer.
    zx_status_t Append(const Bytes& tail);

    // Takes the last |tail->len()| bytes of this buffer, copies them into |tail|, and truncates
    // this buffer.  It is an error for |tail| to be longer than this buffer.
    zx_status_t Split(Bytes* tail);

    // Yields ownership of the underlying buffer and returns it after saving the length in |len| if
    // not null.
    fbl::unique_ptr<uint8_t[]> Release(size_t* len = nullptr);

    // Clears all state from this instance.  This is guaranteed to zeroize and free the underlying
    // buffer if it is allocated.
    void Reset();

    // Array access operators.  Assert that |off| is not out of bounds.
    const uint8_t& operator[](zx_off_t off) const;
    uint8_t& operator[](zx_off_t off);

    // Comparison operators.  These are guaranteed to be constant-time.
    bool operator==(const Bytes& other) const;
    bool operator!=(const Bytes& other) const { return !(*this == other); }

private:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(Bytes);

    // The underlying buffer.  The destructor is guaranteed to zero this buffer if allocated.
    fbl::unique_ptr<uint8_t[]> buf_;
    // Length in bytes of memory currently allocated to the underlying buffer.
    size_t len_;
};

} // namespace crypto
