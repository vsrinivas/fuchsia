// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include <zircon/compiler.h>
#include <zircon/types.h>

#ifndef SHA256_DIGEST_LENGTH
#define SHA256_DIGEST_LENGTH 32
#endif

#define DIGEST_LENGTH SHA256_DIGEST_LENGTH

#ifdef __cplusplus

#include <fbl/unique_ptr.h>

namespace digest {

// This class represents a digest produced by a hash algorithm.
// This class is not thread safe.
class Digest final {
public:
    // The length of a digest in bytes; this matches sizeof(this->data).
    static constexpr size_t kLength = DIGEST_LENGTH;

    Digest();
    explicit Digest(const uint8_t* other);
    explicit Digest(Digest&& o) = delete;
    Digest& operator=(const uint8_t* rhs);
    ~Digest();

    // Initializes the hash algorithm context.  It must be called before Update,
    // and after Final when reusing the Digest object.
    zx_status_t Init();

    // Adds data to be hashed.  This may be called multiple times between calls
    // to |Init| and |Final|.  If A and B are byte sequences of length A_len and
    // B_len, respectively, and AB is the concatenation of A and B, then
    // "Update(A, A_len); Update(B, B_len);" and "Update(AB, A_len + B_len)"
    // yield the same internal state and will produce the same digest when
    // |Final| is called.
    void Update(const void* data, size_t len);

    // Completes the hash algorithm and returns the digest.  This must only be
    // called after a call to |Init|; intervening calls to |Update| are
    // optional.
    const uint8_t* Final();

    // This convenience method performs the hash algorithm in "one shot": It
    // calls |Init| and |Update(data, len)| before returning the result of
    // calling |Final|.
    const uint8_t* Hash(const void* data, size_t len);

    // Converts a |hex| string to binary and stores it in this->data.  The
    // string must contain at least |kLength| * 2 valid hex characters.
    zx_status_t Parse(const char* hex, size_t len);

    // Writes the current digest to |out| as a null-terminated, hex-encoded
    // string.  |out| must have room for (kLength * 2) + 1 characters to
    // succeed.
    zx_status_t ToString(char* out, size_t len) const;

    // Write the current digest to |out|.  |len| must be at least kLength to
    // succeed.
    zx_status_t CopyTo(uint8_t* out, size_t len) const;

    // Returns a pointer to a buffer containing the current digest value.  This
    // will always have |kLength| bytes.  Each call to |AcquireBytes| must have
    // a corresponding call to |ReleaseBytes| before calling any other non-const
    // method.
    const uint8_t* AcquireBytes() const;

    // Indicates to this object that the caller is finished using the pointer
    // returned by |AcquireBytes|.  It is an error to call any other non-const
    // method after |AcquireBytes| without first calling |ReleaseBytes|.
    void ReleaseBytes() const;

    // Equality operators.  Those that take |const uint8_t *| arguments will
    // read |kLength| bytes; callers MUST ensure there are sufficient bytes
    // present.
    bool operator==(const Digest& rhs) const;
    bool operator!=(const Digest& rhs) const;
    bool operator==(const uint8_t* rhs) const;
    bool operator!=(const uint8_t* rhs) const;

private:
    // Opaque crypto implementation context.
    struct Context;

    // Opaque pointer to the crypto implementation context.
    fbl::unique_ptr<Context> ctx_;
    // The raw bytes of the current digest.  This is filled in either by the
    // assignment operators or the Parse and Final methods.
    uint8_t bytes_[kLength];
    // The number of outstanding calls to |AcquireBytes| without matching calls
    // to |ReleaseBytes|.
    mutable size_t ref_count_;
};

} // namespace digest
#endif // __cplusplus

__BEGIN_CDECLS
typedef struct digest_t digest_t;

// C API for Digest.  It's called in a similar manner as the corresponding C++
// code, namely, the caller should do roughly the following (with additional
// error checking):
//     digest_t *digest = NULL;
//     digest_init(&digest);
//     while(true) {
//         // Fill buf with len bytes somehow or break if no more data
//         digest_update(digest, buf, len);
//     }
//     uint8_t result[DIGEST_LENGTH];
//     digest_final(digest, result, sizeof(result));
//
// Then |result| will have the result of the hashing the data.

// C wrapper for |Digest::Init|.  On success this function allocates memory and
// returns it in |out|.  The caller must make a call to |digest_final| to free
// this memory.
zx_status_t digest_init(digest_t** out);

// C wrapper for |Digest::Update|.
void digest_update(digest_t* digest, const void* buf, size_t len);

// C wrapper for |Digest::Final|. |out| must point to a buffer with room for at
// least |DIGEST_LENGTH| bytes.  This method frees |digest|, which must not be
// used again after this call.
zx_status_t digest_final(digest_t* digest, void* out, size_t out_len);

// C wrapper for |Digest::Hash|.  |out| must point to a buffer with room for at
// least |DIGEST_LENGTH| bytes.
zx_status_t digest_hash(const void* buf, size_t len, void* out, size_t out_len);

__END_CDECLS
