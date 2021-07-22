// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_FCRYPTO_RUST_FFI_H_
#define SRC_SECURITY_FCRYPTO_RUST_FFI_H_

#include <zircon/status.h>

#include "src/security/fcrypto/cipher.h"

// Header generated from Rust cxxbridge declarations.  Include dir added by build template.
#include "src/security/fcrypto-rust/ffi.rs.h"

namespace crypto {

std::unique_ptr<Cipher> new_cipher();

zx_status_t init_for_encipher(Cipher& cipher, rust::Slice<const uint8_t> secret,
                              rust::Slice<const uint8_t> iv, uint64_t alignment);
zx_status_t init_for_decipher(Cipher& cipher, rust::Slice<const uint8_t> secret,
                              rust::Slice<const uint8_t> iv, uint64_t alignment);

zx_status_t encipher(Cipher& cipher, rust::Slice<const uint8_t> plaintext, uint64_t offset,
                     rust::Slice<uint8_t> ciphertext);
zx_status_t decipher(Cipher& cipher, rust::Slice<const uint8_t> ciphertext, uint64_t offset,
                     rust::Slice<uint8_t> plaintext);

}  // namespace crypto

#endif  // SRC_SECURITY_FCRYPTO_RUST_FFI_H_
