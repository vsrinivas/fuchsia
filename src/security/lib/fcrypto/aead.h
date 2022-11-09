// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FCRYPTO_AEAD_H_
#define SRC_SECURITY_LIB_FCRYPTO_AEAD_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <memory>

#include <fbl/macros.h>

#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/cipher.h"
#include "src/security/lib/fcrypto/secret.h"

// |crypto::AEAD| is an authenticated encryption and decryption cipher.  It differs from |Cipher| in
// that it incurs additional overhead to store its authentication tag, but can verify data integrity
// as a result.  The ciphertext produced by an AEAD is the same length as its plaintext, excluding
// the IV and tag.  A 64 bit nonce is used to seal plain texts, meaning a given key and IV can be
// used for at most 2^64 - 1 operations.
namespace crypto {

class __EXPORT AEAD final {
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
  zx_status_t InitSeal(Algorithm aead, const Secret& key, const Bytes& iv) {
    return Init(aead, key, iv, Cipher::kEncrypt);
  }

  // Sets up the AEAD to use the algorithm indicated by |aead| to decrypt data using the given
  // |key| and |iv|.
  zx_status_t InitOpen(Algorithm aead, const Secret& key, const Bytes& iv) {
    return Init(aead, key, iv, Cipher::kDecrypt);
  }

  // Encrypts data from |ptext| to |ctext|, based on the parameters set in |InitSeal|.  Saves the
  // |iv| used;  |iv| will be resized and filled automatically.  The AEAD tag is stored at the end
  // of |ctext|  This method will fail if called 2^64 or more times with the same key and IV.  The
  // second variant includes additional authenticated data in the tag calculation.
  zx_status_t Seal(const Bytes& ptext, uint64_t* out_nonce, Bytes* out_ctext) {
    return Seal(ptext, nullptr, 0, out_nonce, out_ctext);
  }
  zx_status_t Seal(const Bytes& ptext, const Bytes& aad, uint64_t* out_nonce, Bytes* out_ctext) {
    return Seal(ptext, aad.get(), aad.len(), out_nonce, out_ctext);
  }

  // Decrypts data from |ctext| to |ptext|, based on the parameters set in |InitOpen|.
  // Decryption can only succeed if the |iv| matches those produced by |Seal| and the AEAD tag is
  // included in |ctext|.  The second variant includes additional authenticated data in the tag
  // calculation.
  zx_status_t Open(uint64_t nonce, const Bytes& ctext, Bytes* out_ptext) {
    return Open(nonce, ctext, nullptr, 0, out_ptext);
  }
  zx_status_t Open(uint64_t nonce, const Bytes& ctext, const Bytes& aad, Bytes* out_ptext) {
    return Open(nonce, ctext, aad.get(), aad.len(), out_ptext);
  }

  // Clears all state from this instance.
  void Reset();

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(AEAD);

  // Sets up the aead to encrypt or decrypt data using the given |key| based on the
  // given |direction|.
  zx_status_t Init(Algorithm aead, const Secret& key, const Bytes& iv, Cipher::Direction direction);
  zx_status_t Seal(const Bytes& ptext, const uint8_t* aad, size_t aad_len, uint64_t* out_nonce,
                   Bytes* out_ctext);
  zx_status_t Open(uint64_t nonce, const Bytes& ctext, const uint8_t* aad, size_t aad_len,
                   Bytes* out_ptext);

  // Opaque crypto implementation context.
  struct Context;

  // Opaque pointer to the crypto implementation context.
  std::unique_ptr<Context> ctx_;
  // Indicates whether configured to encrypt or decrypt.
  Cipher::Direction direction_;
  // Buffer holding initial vector.  The IV is expanded to be |uint64_t|-aligned.
  std::unique_ptr<uint64_t[]> iv_;
  // Original value of |iv_[0]|.  |Seal| will fail if |iv_[0]| wraps around to this value.
  uint64_t iv0_;
  // Size of the actual IV.
  size_t iv_len_;
  // Length of authentication tag.
  size_t tag_len_;
};
}  // namespace crypto

#endif  // SRC_SECURITY_LIB_FCRYPTO_AEAD_H_
