// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SECURITY_LIB_FCRYPTO_HKDF_H_
#define SRC_SECURITY_LIB_FCRYPTO_HKDF_H_

#include <stddef.h>
#include <stdint.h>
#include <zircon/types.h>

#include <fbl/macros.h>

#include "src/security/lib/fcrypto/bytes.h"
#include "src/security/lib/fcrypto/digest.h"
#include "src/security/lib/fcrypto/secret.h"

// |crypto::HKDF| is a key derivation function.  It can turn variable-length and/or weak input key
// material into cryptographically strong output key material.  However, this class does NOT do any
// key stretching.  It is the caller's responsibility to protect against brute forcing weak input
// key material, e.g. by requiring strong input key material or by rate-limiting the use of both
// |Init| and |Derive|.
namespace crypto {

class __EXPORT HKDF final {
 public:
  enum Flags : uint16_t {
    ALLOW_WEAK_KEY = 0x0001,
  };

  HKDF();
  ~HKDF();

  // Initializes the HKDF algorithms indicated by |digest| with the input key material in |ikm|
  // and the given |salt|.  Callers must omit |flags| unless the security implications are clearly
  // understood.
  zx_status_t Init(digest::Algorithm digest, const Secret& ikm, const Bytes& salt,
                   uint16_t flags = 0);

  // Fill |out| with |len| bytes of output key material.  The key material will depend on the
  // |ikm| and |salt| given in |Init|, as well as the |label| provided here.  |out_key| will be
  // the same if and only if all of those parameters are unchanged.
  zx_status_t Derive(const char* label, size_t len, Bytes* out);
  zx_status_t Derive(const char* label, size_t len, Secret* out);

 private:
  DISALLOW_COPY_ASSIGN_AND_MOVE(HKDF);

  // Implementation of |Derive| above.
  zx_status_t Derive(const char* label, uint8_t* out, size_t out_len);

  // Digest algorithm used by this HKDF.
  digest::Algorithm digest_;
  // The pseudo-random key used to derive other keys
  Secret prk_;
};

}  // namespace crypto

#endif  // SRC_SECURITY_LIB_FCRYPTO_HKDF_H_
