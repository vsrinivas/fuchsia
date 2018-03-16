// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <fbl/macros.h>
#include <zircon/types.h>

// |crypto::Cipher| is used to encrypt and decrypt data.  Ciphers differ from AEADs in that they
// require block-aligned lengths and do not check data integrity.  This implementation can be used
// as either a stream cipher, or as a random access cipher if the cipher has a "tweaked codebook
// mode".  See the variants of |Init| and |Transform| for details.
namespace crypto {

class Cipher final {
public:
    // Algorithm enumerates the supported secret key ciphers.
    enum Algorithm {
        kUninitialized = 0,
        kAES128_CTR, // NOT a "tweaked" cipher; this MUST NOT be used for disk encryption
        kAES256_XTS, // A "tweaked cipher
    };

    // Indicates whether the objects turns plaintext into ciphertext or vice versa.
    enum Direction {
        kUnset = 0,
        kEncrypt,
        kDecrypt,
    };

    Cipher();
    ~Cipher();

    Direction direction() const { return direction_; }
    uint64_t alignment() const { return alignment_; }

    // Gets the number of bytes needed for the symmetric key used by the given |cipher|.
    static zx_status_t GetKeyLen(Algorithm cipher, size_t* out);

    // Gets the number of bytes needed for the initialization vector (IV) used by the given
    // |cipher|.
    static zx_status_t GetIVLen(Algorithm cipher, size_t* out);

    // Gets the cipher block size in bytes for  the given |cipher|.  Data passed to |Encrypt| or
    // |Decrypt| must be a multiple of this size.
    static zx_status_t GetBlockSize(Algorithm cipher, size_t* out);

    // Sets up the cipher to encrypt or decrypt data using the given |key| and |iv|,  based on the
    // given |direction|, either as:
    //   - A stream ciphers, using the first variant that omits the |alignment|.
    //   - As a random access cipher, using the second variant.  All offsets must be
    //   |alignment|-aligned, and |alignment| must be a power of 2.
    zx_status_t Init(Algorithm algo, Direction direction, const Bytes& key, const Bytes& iv,
                     uint64_t alignment);

    // Sets up the cipher to encrypt data using the given |key| and |iv|, either as a stream cipher
    // or a random access cipher, as described above in |Init|.
    zx_status_t InitEncrypt(Algorithm algo, const Bytes& key, const Bytes& iv) {
        return Init(algo, kEncrypt, key, iv, 0);
    }
    zx_status_t InitEncrypt(Algorithm algo, const Bytes& key, const Bytes& iv, uint64_t alignment) {
        return Init(algo, kEncrypt, key, iv, alignment);
    }

    // Sets up the cipher to decrypt data using the given |key| and |iv|, either as a stream cipher
    // or a random access cipher, as described above in |Init|.
    zx_status_t InitDecrypt(Algorithm algo, const Bytes& key, const Bytes& iv) {
        return Init(algo, kDecrypt, key, iv, 0);
    }
    zx_status_t InitDecrypt(Algorithm algo, const Bytes& key, const Bytes& iv, uint64_t alignment) {
        return Init(algo, kDecrypt, key, iv, alignment);
    }

    // Encrypts or decrypts |length| bytes from |in| to |out|, based on the given |direction| and
    // the parameters set in |Init|:
    //  - Must have been configured with the same |direction|.
    //  - If |alignment| was non-zero, |offset| must be a multiple of it.
    // Finally, |length| must be a multiple of cipher blocks, and |out| must have room for |length|
    // bytes.
    zx_status_t Transform(const uint8_t* in, zx_off_t offset, size_t length, uint8_t* out,
                          Direction Direction);

    // Encrypts |len| bytes from |in| to |out|, as described above in |Transform|.
    zx_status_t Encrypt(const uint8_t* in, size_t length, uint8_t* out) {
        return Transform(in, 0, length, out, kEncrypt);
    }
    zx_status_t Encrypt(const uint8_t* in, zx_off_t offset, size_t length, uint8_t* out) {
        return Transform(in, offset, length, out, kEncrypt);
    }

    // Decrypts |len| bytes from |in| to |out|, as described above in |Transform|.
    zx_status_t Decrypt(const uint8_t* in, size_t length, uint8_t* out) {
        return Transform(in, 0, length, out, kDecrypt);
    }
    zx_status_t Decrypt(const uint8_t* in, zx_off_t offset, size_t length, uint8_t* out) {
        return Transform(in, offset, length, out, kDecrypt);
    }

    // Clears all state from this instance.
    void Reset();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Cipher);

    // Opaque crypto implementation context.
    struct Context;

    // Opaque pointer to the crypto implementation context.
    fbl::unique_ptr<Context> ctx_;
    // Indicates which algorithm to use to transform data.
    Algorithm cipher_;
    // Indicates whether configured to encrypt or decrypt.
    Direction direction_;
    // Cipher block size.
    size_t block_size_;
    // Buffer used to hold the initial vector.
    Bytes iv_;
    // Buffer used to hold the tweaked initial vector.
    Bytes tweaked_iv_;
    // Indicates how offsets
    uint64_t alignment_;
};
} // namespace crypto
