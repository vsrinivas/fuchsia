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
// require block-aligned lengths and do not check data integrity.
namespace crypto {

class Cipher final {
public:
    // Algorithm enumerates the supported secret key ciphers.
    enum Algorithm {
        kUninitialized = 0,
        kAES256_XTS,
    };

    // Indicates whether the objects turns plaintext into ciphertext or vice versa.
    enum Direction {
        kUnset = 0,
        kEncrypt,
        kDecrypt,
    };

    Cipher();
    ~Cipher();

    // Gets the number of bytes needed for the symmetric key used by the given |cipher|.
    static zx_status_t GetKeyLen(Algorithm cipher, size_t* out);

    // Gets the number of bytes needed for the initialization vector (IV) used by the given
    // |cipher|.
    static zx_status_t GetIVLen(Algorithm cipher, size_t* out);

    // Gets the cipher block size in bytes for  the given |cipher|.  Data passed to |Encrypt| or
    // |Decrypt| must be a multiple of this size.
    static zx_status_t GetBlockSize(Algorithm cipher, size_t* out);

    // Sets up the cipher to encrypt data using the given |key| and |iv|.  The |tweakable| mask
    // indicates which bits, if any, are used as an offset that can be set by |Tweak|.  A value of
    // zero means the IV cannot be adjusted.
    zx_status_t InitEncrypt(Algorithm cipher, const Bytes& key, const Bytes& iv,
                            uint64_t tweakable = 0);

    // Sets up the cipher to decrypt data using the given |key| and |iv|.  The |tweakable| mask
    // indicates which bits, if any, are used as an offset that can be set by |Tweak|.  A value of
    // zero means the IV cannot be adjusted.
    zx_status_t InitDecrypt(Algorithm cipher, const Bytes& key, const Bytes& iv,
                            uint64_t tweakable = 0);

    // Sets |out| to this object's direction.  Returns |ZX_ERR_BAD_STATE| if it has not been
    // configured.
    zx_status_t GetDirection(Direction* out) const;

    // Adjusts the "tweakable" bits of the IV to "seek" to a particular data |offset|.  The |offset|
    // must fall within the mask set in either |InitEncrypt| or |InitDecrypt|.
    zx_status_t Tweak(uint64_t offset);

    // Encrypts |len| bytes from |in| to |out|, based on the parameters set in
    // |InitEncrypt|.  Returns |ZX_ERR_BAD_STATE| if configured to decrypt and |ZX_ERR_INVALID_ARGS|
    // if |len| is not a multiple of cipher blocks.
    zx_status_t Encrypt(const uint8_t* in, size_t len, uint8_t* out);

    // Decrypts |len| bytes from |in| to |out|, based on the parameters set in
    // |InitDecrypt|.  Returns |ZX_ERR_BAD_STATE| if configured to encrypt and |ZX_ERR_INVALID_ARGS|
    // if |in_len| is not a multiple of cipher blocks.
    zx_status_t Decrypt(const uint8_t* in, size_t len, uint8_t* out);

    // Clears all state from this instance.
    void Reset();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Cipher);

    // Sets up the cipher to encrypt or decrypt data using the given |key| and |iv| based on the
    // given |direction|.
    zx_status_t Init(Algorithm cipher, const Bytes& key, const Bytes& iv, uint64_t tweakable,
                     Direction Direction);

    // Encrypts or decrypts |in_len| bytes from |in| to |out|, based on the parameters set in
    // |Init|.  Sets |actual| to number of bytes written to |out|.
    zx_status_t Transform(const uint8_t* in, size_t in_len, uint8_t* out);

    // Opaque crypto implementation context.
    struct Context;

    // Opaque pointer to the crypto implementation context.
    fbl::unique_ptr<Context> ctx_;
    // Indicates which algorithm to use to transform data.
    Algorithm cipher_;
    // Indicates whether configured to encrypt or decrypt.
    Direction direction_;
    // Buffer used to hold the initial vector.
    Bytes iv_;
    // Cipher block size.
    size_t block_size_;
    // Bit mask of how much of the IV is reserved for an adjustable counter.
    uint64_t tweakable_;
};
} // namespace crypto
