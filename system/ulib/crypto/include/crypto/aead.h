// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#include <crypto/bytes.h>
#include <crypto/cipher.h>
#include <fbl/macros.h>
#include <zircon/types.h>

// |crypto::AEAD| is an authenticated encryption and decryption cipher.  It differs from |Cipher| in
// that it incurs additional overhead to store its authentication tag, but can verify data integrity
// as a result.  The ciphertext produced by an AEAD is the same length as its plaintext, excluding
// the IV and tag.
namespace crypto {

class AEAD final {
public:
    // Algorithm enumerates the supported secret key ciphers.
    enum Algorithm {
        kUninitialized = 0,
        kAES128_GCM,
        kAES128_GCM_SIV,
    };

    AEAD();
    ~AEAD();

    // Gets the number of bytes needed for the symmetric key used by the given |aead|.
    static zx_status_t GetKeyLen(Algorithm aead, size_t* out);

    // Gets the number of bytes needed for the initialization vector (IV) used by the given
    // |aead|.
    static zx_status_t GetIVLen(Algorithm aead, size_t* out);

    // Gets the length of an authentication tag created by the given |aead|.
    static zx_status_t GetTagLen(Algorithm aead, size_t* out);

    // Sets up the AEAD to use the algorithm indicated by |aead| to encrypt data using the given
    // |key| and |iv|.
    zx_status_t InitSeal(Algorithm aead, const Bytes& key, const Bytes& iv);

    // Sets up the AEAD to use the algorithm indicated by |aead| to decrypt data using the given
    // |key|.
    zx_status_t InitOpen(Algorithm aead, const Bytes& key);

    // Copies the additional authenticated data from the given |ad|.
    zx_status_t SetAD(const Bytes& ad);

    // Allocates |ad_len| bytes and returns a handle to the region in |out_ad|.  This memory will be
    // included as additional authenticated data each time data is |Seal|ed or |Open|ed, and may be
    // modified between those calls.
    zx_status_t AllocAD(size_t ad_len, uintptr_t* out_ad);

    // Encrypts data from |ptext| to |ctext|, based on the parameters set in |InitSeal|.  Saves the
    // |iv| used;  |iv| will be resized and filled automatically.  The AEAD tag is stored at the end
    // of |ctext|  The current contents of the additional authenticated data region set up in
    // |InitSeal| are included in the tag calculation.
    zx_status_t Seal(const Bytes& ptext, Bytes* iv, Bytes* ctext);

    // Decrypts data from |ctext| to |ptext|, based on the parameters set in |InitOpen|.
    // Decryption can only succeed if the |iv| matches those produced by |Seal| and the AEAD tag is
    // included in |ctext|.  The current contents of the additional authenticated data region set up
    // in |InitSeal| are included in the tag calculation.
    zx_status_t Open(const Bytes& iv, const Bytes& ctext, Bytes* ptext);

    // Clears all state from this instance.
    void Reset();

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(AEAD);

    // Sets up the aead to encrypt or decrypt data using the given |key| based on the
    // given |direction|.
    zx_status_t Init(Algorithm aead, const Bytes& key, Cipher::Direction direction);

    // Opaque crypto implementation context.
    struct Context;

    // Opaque pointer to the crypto implementation context.
    fbl::unique_ptr<Context> ctx_;
    // Indicates whether configured to encrypt or decrypt.
    Cipher::Direction direction_;
    // Buffer and length used to hold the initial vector.
    Bytes iv_;
    // Additional authenticated data (AAD).
    Bytes ad_;
    // Length of authentication tag.
    size_t tag_len_;
};
} // namespace crypto
